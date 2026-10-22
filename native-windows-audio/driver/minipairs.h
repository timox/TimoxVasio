// Timox endpoint descriptions. Derived from Microsoft SysVAD table conventions;
// see upstream/LICENSE (MS-PL) and THIRD_PARTY_NOTICES.md.
#pragma once

NTSTATUS CreateMiniportWaveRTSYSVAD(PUNKNOWN*, REFCLSID, PUNKNOWN, POOL_FLAGS, PUNKNOWN, PVOID, PENDPOINT_MINIPAIR);
NTSTATUS CreateMiniportTopologySYSVAD(PUNKNOWN*, REFCLSID, PUNKNOWN, POOL_FLAGS, PUNKNOWN, PVOID, PENDPOINT_MINIPAIR);

// The first prototype has one physical stream per endpoint. Windows shared
// mode mixes/fans out applications; the kernel driver does not mix clients.
static KSDATAFORMAT_WAVEFORMATEXTENSIBLE TimoxFormat = {
    {sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE),0,0,0,
     STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM),
     STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)},
    {{WAVE_FORMAT_EXTENSIBLE,2,48000,384000,8,32,sizeof(WAVEFORMATEXTENSIBLE)-sizeof(WAVEFORMATEX)},
     32,KSAUDIO_SPEAKER_STEREO,STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM)}
};
static MODE_AND_DEFAULT_FORMAT TimoxModes[] = {
    {STATIC_AUDIO_SIGNALPROCESSINGMODE_DEFAULT,&TimoxFormat.DataFormat},
    {STATIC_AUDIO_SIGNALPROCESSINGMODE_RAW,&TimoxFormat.DataFormat}
};
static KSDATARANGE_AUDIO TimoxStreamRange = {
    {sizeof(KSDATARANGE_AUDIO),KSDATARANGE_ATTRIBUTES,0,0,
     STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),STATICGUIDOF(KSDATAFORMAT_SUBTYPE_PCM),
     STATICGUIDOF(KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)},2,32,32,48000,48000
};
static KSDATARANGE TimoxBridgeRange = {
    sizeof(KSDATARANGE),0,0,0,STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
    STATICGUIDOF(KSDATAFORMAT_SUBTYPE_ANALOG),STATICGUIDOF(KSDATAFORMAT_SPECIFIER_NONE)
};
static PKSDATARANGE TimoxStreamRanges[] = {PKSDATARANGE(&TimoxStreamRange),PKSDATARANGE(&PinDataRangeAttributeList)};
static PKSDATARANGE TimoxBridgeRanges[] = {&TimoxBridgeRange};
static PIN_DEVICE_FORMATS_AND_MODES TimoxRenderFormats[] = {
    {SystemRenderPin,&TimoxFormat,1,TimoxModes,SIZEOF_ARRAY(TimoxModes)},
    {BridgePin,NULL,0,NULL,0}
};
static PIN_DEVICE_FORMATS_AND_MODES TimoxCaptureFormats[] = {
    {BridgePin,NULL,0,NULL,0},
    {SystemCapturePin,&TimoxFormat,1,TimoxModes,SIZEOF_ARRAY(TimoxModes)}
};
static PCPIN_DESCRIPTOR TimoxRenderPins[] = {
    {1,1,0,NULL,{0,NULL,0,NULL,SIZEOF_ARRAY(TimoxStreamRanges),TimoxStreamRanges,
        KSPIN_DATAFLOW_IN,KSPIN_COMMUNICATION_SINK,&KSCATEGORY_AUDIO,NULL,0}},
    {0,0,0,NULL,{0,NULL,0,NULL,1,TimoxBridgeRanges,
        KSPIN_DATAFLOW_OUT,KSPIN_COMMUNICATION_NONE,&KSCATEGORY_AUDIO,NULL,0}}
};
static PCPIN_DESCRIPTOR TimoxCapturePins[] = {
    {0,0,0,NULL,{0,NULL,0,NULL,1,TimoxBridgeRanges,
        KSPIN_DATAFLOW_IN,KSPIN_COMMUNICATION_NONE,&KSCATEGORY_AUDIO,NULL,0}},
    {1,1,0,NULL,{0,NULL,0,NULL,SIZEOF_ARRAY(TimoxStreamRanges),TimoxStreamRanges,
        KSPIN_DATAFLOW_OUT,KSPIN_COMMUNICATION_SINK,&KSCATEGORY_AUDIO,NULL,0}}
};
static PCCONNECTION_DESCRIPTOR TimoxDirectConnection[] = {{PCFILTER_NODE,0,PCFILTER_NODE,1}};
static PCPROPERTY_ITEM TimoxWaveProperties[] = {
    {&KSPROPSETID_Pin,KSPROPERTY_PIN_PROPOSEDATAFORMAT,
        KSPROPERTY_TYPE_SET|KSPROPERTY_TYPE_BASICSUPPORT,PropertyHandler_WaveFilter},
    {&KSPROPSETID_Pin,KSPROPERTY_PIN_PROPOSEDATAFORMAT2,
        KSPROPERTY_TYPE_GET|KSPROPERTY_TYPE_BASICSUPPORT,PropertyHandler_WaveFilter}
};
DEFINE_PCAUTOMATION_TABLE_PROP(TimoxWaveAutomation,TimoxWaveProperties);
static PCFILTER_DESCRIPTOR TimoxRenderWave = {
    0,&TimoxWaveAutomation,sizeof(PCPIN_DESCRIPTOR),2,TimoxRenderPins,
    sizeof(PCNODE_DESCRIPTOR),0,NULL,1,TimoxDirectConnection,0,NULL
};
static PCFILTER_DESCRIPTOR TimoxCaptureWave = {
    0,&TimoxWaveAutomation,sizeof(PCPIN_DESCRIPTOR),2,TimoxCapturePins,
    sizeof(PCNODE_DESCRIPTOR),0,NULL,1,TimoxDirectConnection,0,NULL
};
static PCPIN_DESCRIPTOR TimoxRenderTopoPins[] = {
    {0,0,0,NULL,{0,NULL,0,NULL,1,TimoxBridgeRanges,KSPIN_DATAFLOW_IN,KSPIN_COMMUNICATION_NONE,&KSCATEGORY_AUDIO,NULL,0}},
    {0,0,0,NULL,{0,NULL,0,NULL,1,TimoxBridgeRanges,KSPIN_DATAFLOW_OUT,KSPIN_COMMUNICATION_NONE,&KSNODETYPE_SPEAKER,NULL,0}}
};
static PCPIN_DESCRIPTOR TimoxCaptureTopoPins[] = {
    {0,0,0,NULL,{0,NULL,0,NULL,1,TimoxBridgeRanges,KSPIN_DATAFLOW_IN,KSPIN_COMMUNICATION_NONE,&KSNODETYPE_MICROPHONE,NULL,0}},
    {0,0,0,NULL,{0,NULL,0,NULL,1,TimoxBridgeRanges,KSPIN_DATAFLOW_OUT,KSPIN_COMMUNICATION_NONE,&KSCATEGORY_AUDIO,NULL,0}}
};
static PCFILTER_DESCRIPTOR TimoxRenderTopo = {
    0,NULL,sizeof(PCPIN_DESCRIPTOR),2,TimoxRenderTopoPins,sizeof(PCNODE_DESCRIPTOR),0,NULL,1,TimoxDirectConnection,0,NULL
};
static PCFILTER_DESCRIPTOR TimoxCaptureTopo = {
    0,NULL,sizeof(PCPIN_DESCRIPTOR),2,TimoxCaptureTopoPins,sizeof(PCNODE_DESCRIPTOR),0,NULL,1,TimoxDirectConnection,0,NULL
};
static PHYSICALCONNECTIONTABLE TimoxRenderConnection[] = {{0,1,CONNECTIONTYPE_WAVE_OUTPUT}};
static PHYSICALCONNECTIONTABLE TimoxCaptureConnection[] = {{1,0,CONNECTIONTYPE_TOPOLOGY_OUTPUT}};
static ENDPOINT_MINIPAIR TimoxRender = {
    eSpeakerDevice,L"TimoxRenderTopology",NULL,CreateMiniportTopologySYSVAD,&TimoxRenderTopo,0,NULL,
    L"TimoxRenderWave",NULL,CreateMiniportWaveRTSYSVAD,&TimoxRenderWave,0,NULL,
    2,TimoxRenderFormats,SIZEOF_ARRAY(TimoxRenderFormats),TimoxRenderConnection,1,ENDPOINT_NO_FLAGS,NULL,0,NULL
};
static ENDPOINT_MINIPAIR TimoxCapture = {
    eMicInDevice,L"TimoxCaptureTopology",NULL,CreateMiniportTopologySYSVAD,&TimoxCaptureTopo,0,NULL,
    L"TimoxCaptureWave",NULL,CreateMiniportWaveRTSYSVAD,&TimoxCaptureWave,0,NULL,
    2,TimoxCaptureFormats,SIZEOF_ARRAY(TimoxCaptureFormats),TimoxCaptureConnection,1,ENDPOINT_NO_FLAGS,NULL,0,NULL
};
PENDPOINT_MINIPAIR g_RenderEndpoints[] = {&TimoxRender};
PENDPOINT_MINIPAIR g_CaptureEndpoints[] = {&TimoxCapture};
#define g_cRenderEndpoints SIZEOF_ARRAY(g_RenderEndpoints)
#define g_cCaptureEndpoints SIZEOF_ARRAY(g_CaptureEndpoints)
#define g_MaxMiniports ((g_cRenderEndpoints + g_cCaptureEndpoints) * 2)
