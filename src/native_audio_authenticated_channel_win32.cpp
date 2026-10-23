#include "native_audio_authenticated_channel_win32.h"
#include "schannel_stream_win32.h"
#include "pipe_identity_win32.h"
#include "channel_frames.h"
#include <algorithm>
#include <atomic>
#include <cwchar>
#include <cstring>
namespace {
using namespace timox_broker;
using Code=NativeChannelCode;
class Channel final:public INativeAudioAuthenticatedChannel {
public:
    Channel(const NativeAudioChannelCredentials& credentials,const wchar_t* name){
        const auto length=name?wcsnlen_s(name,180):0;
        valid_=length>0&&length<180&&credentials.clientCertificate&&
            std::any_of(credentials.serverCertificateSha256.begin(),credentials.serverCertificateSha256.end(),[](auto byte){return byte!=0;});
        if(length>0&&length<180)std::copy_n(name,length,bootstrapName_.data());
        auto bootstrap=std::make_unique<PipeStream>();bootstrapPipe_=bootstrap.get();
        auto data=std::make_unique<PipeStream>();dataPipe_=data.get();
        bootstrap_=std::make_unique<SchannelStream>(std::move(bootstrap),false,credentials.clientCertificate,credentials.serverCertificateSha256);
        data_=std::make_unique<SchannelStream>(std::move(data),false,credentials.clientCertificate,credentials.serverCertificateSha256);
    }
    ~Channel()override{RequestCancel();Close();}
    NativeChannelResult Connect(Deadline deadline)noexcept override{
        if(Cancelled())return Fail({Code::Cancelled});
        if(state_!=State::Disconnected)return Fail({Code::Failed,0,ERROR_INVALID_STATE});
        if(!valid_)return Fail({Code::AccessDenied,0,ERROR_INVALID_PARAMETER});
        const auto error=ObserveProcessIdentity(identity_);
        if(error)return Fail({Code::AccessDenied,0,error});
        auto result=bootstrapPipe_->Connect(bootstrapName_.data(),deadline);
        if(result.code!=Code::Ok)return Fail(result);
        result=bootstrap_->Handshake(deadline);if(result.code!=Code::Ok)return Fail(result);
        ChannelBinding request;request.type=BindingType::BindRequest;request.identity=identity_;
        result=Bind(*bootstrap_,request,BindingType::BindReply,deadline);
        if(result.code!=Code::Ok)return result;
        state_=State::BootstrapBound;return {Code::Ok};
    }
    NativeChannelResult ConfirmLease(std::uint64_t lease,std::uint64_t generation,Deadline deadline)noexcept override{
        if(Cancelled())return Fail({Code::Cancelled});
        if(state_!=State::LeasePending||lease!=lease_||generation!=generation_)
            return Fail({Code::Failed,0,ERROR_INVALID_STATE});
        ChannelBinding offer;
        auto result=ReadBindingFrame(*bootstrap_,offer,deadline);
        if(result.code!=Code::Ok)return Fail(result);
        if(offer.type!=BindingType::DataOffer||offer.lease!=lease_||offer.generation!=generation_)
            return Fail({Code::AccessDenied});
        ChannelIdentity current;
        const auto error=ObserveProcessIdentity(current);
        if(error||!SameChannelIdentity(current,identity_))return Fail({Code::AccessDenied,0,error});
        std::array<wchar_t,240> name{};
        const auto length=std::wcslen(bootstrapName_.data());std::copy_n(bootstrapName_.data(),length,name.data());
        constexpr wchar_t suffix[]=L".data.";std::copy_n(suffix,6,name.data()+length);
        for(std::size_t n=0;n<offer.nonce.size();++n){name[length+6+2*n]=L"0123456789abcdef"[offer.nonce[n]>>4];name[length+7+2*n]=L"0123456789abcdef"[offer.nonce[n]&15];}
        result=dataPipe_->Connect(name.data(),deadline);if(result.code!=Code::Ok)return Fail(result);
        result=data_->Handshake(deadline);if(result.code!=Code::Ok)return Fail(result);
        offer.type=BindingType::DataBindRequest;offer.identity=identity_;
        result=Bind(*data_,offer,BindingType::DataBindReply,deadline);
        if(result.code!=Code::Ok)return result;
        // Bootstrap stays alive until the second peer has authenticated the
        // exact original identity/lease/generation/nonce. No ownership transfer.
        bootstrap_->Close();state_=State::DataBound;return {Code::Ok};
    }
    NativeChannelResult Exchange(const void* request,std::size_t requestBytes,void* response,std::size_t capacity,Deadline deadline)noexcept override{
        const auto fail=[&](NativeChannelResult result){
            if(response&&capacity<=MaxMessageBytes)std::memset(response,0,capacity);
            return Fail(result);
        };
        if(Cancelled())return fail({Code::Cancelled});
        if((state_!=State::BootstrapBound&&state_!=State::DataBound)||!request||requestBytes<HeaderBytes||
            requestBytes>MaxMessageBytes||!response||capacity<HeaderBytes||capacity>MaxMessageBytes)
            return fail({Code::Failed,0,ERROR_INVALID_PARAMETER});
        const auto* bytes=static_cast<const unsigned char*>(request);
        const auto opcode=static_cast<Opcode>(frames_detail::Read(bytes,6,2));
        if((state_==State::BootstrapBound)!=(opcode==Opcode::Session))return fail({Code::AccessDenied});
        Context context;context.opcode=opcode;context.lastRequestId=lastRequest_;context.leaseId=lease_;context.generation=generation_;
        const auto decoded=ValidateMessage(request,requestBytes,context);
        if(decoded.error!=Error::None)return fail({Code::Failed,0,ERROR_INVALID_DATA});
        context.response=true;context.requestId=decoded.message.requestId;
        if(opcode==Opcode::ReadRender)context.maxFrames=static_cast<std::uint32_t>(frames_detail::Read(bytes,56,4));
        auto& stream=state_==State::BootstrapBound?*bootstrap_:*data_;
        auto result=stream.WriteAll(request,requestBytes,deadline);
        if(result.code!=Code::Ok||result.bytes!=requestBytes)return fail(result.code==Code::Ok?NativeChannelResult{Code::Failed}:result);
        result=ReadAudioFrame(stream,response,capacity,deadline);
        if(result.code!=Code::Ok)return fail(result);
        const auto reply=ValidateMessage(response,result.bytes,context);
        if(reply.error!=Error::None)return fail({Code::Failed,0,ERROR_INVALID_DATA});
        if(Cancelled())return fail({Code::Cancelled});
        lastRequest_=context.requestId;
        if(opcode==Opcode::Session){
            if(reply.message.status==Status::Ok){
                lease_=reply.message.leaseId;generation_=frames_detail::Read(reply.message.payload,0,8);state_=State::LeasePending;
            }else Close();
        }else if(opcode==Opcode::Close)Close();
        return result;
    }
    void RequestCancel()noexcept override{
        cancelled_.store(true,std::memory_order_release);
        // Pointers and stream lifetimes never change after construction.
        bootstrap_->RequestCancel();data_->RequestCancel();
    }
    void Close()noexcept override{
        bootstrap_->Close();data_->Close();state_=State::Closed;
        lease_=generation_=lastRequest_=0;identity_={};
    }
private:
    enum class State {Disconnected,BootstrapBound,LeasePending,DataBound,Closed};
    bool Cancelled()const noexcept{return cancelled_.load(std::memory_order_acquire);}
    NativeChannelResult Fail(NativeChannelResult result)noexcept{Close();result.bytes=0;return result;}
    NativeChannelResult Bind(SchannelStream& stream,ChannelBinding expected,BindingType replyType,Deadline deadline)noexcept{
        auto result=WriteBindingFrame(stream,expected,deadline);if(result.code!=Code::Ok)return Fail(result);
        ChannelBinding received;result=ReadBindingFrame(stream,received,deadline);if(result.code!=Code::Ok)return Fail(result);
        expected.type=replyType;
        if(!MatchesBinding(received,expected))return Fail({Code::AccessDenied});
        if(Cancelled())return Fail({Code::Cancelled});
        return {Code::Ok};
    }
    std::atomic<bool> cancelled_{false};bool valid_=false;State state_=State::Disconnected;
    std::array<wchar_t,180> bootstrapName_{};
    PipeStream* bootstrapPipe_=nullptr;PipeStream* dataPipe_=nullptr;
    std::unique_ptr<SchannelStream> bootstrap_,data_;
    ChannelIdentity identity_{};
    std::uint64_t lease_=0,generation_=0,lastRequest_=0;
};
}
std::unique_ptr<INativeAudioAuthenticatedChannel> CreateWindowsNativeAudioAuthenticatedChannel(const NativeAudioChannelCredentials& credentials){
    return std::make_unique<Channel>(credentials,L"\\\\.\\pipe\\Timox.NativeAudio.v1");
}
#if defined(TIMOX_NATIVE_CHANNEL_TEST_ENDPOINT)
std::unique_ptr<INativeAudioAuthenticatedChannel> CreateWindowsNativeAudioAuthenticatedChannelForTest(const NativeAudioChannelCredentials& credentials,const wchar_t* name){return std::make_unique<Channel>(credentials,name);}
std::size_t WindowsNativeAudioChannelRetainedBytesForTest() noexcept{
    return sizeof(Channel)+2*SchannelStream::RetainedApplicationBytes()+2*PipeStream::RetainedApplicationBytes();
}
#endif
