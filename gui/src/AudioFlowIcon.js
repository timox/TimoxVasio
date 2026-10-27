import React from 'react';

const paths = {
    input: <><rect x="25" y="8" width="12" height="24" rx="2" /><path d="M3 20h18m-6-6 6 6-6 6" /></>,
    output: <><rect x="3" y="8" width="12" height="24" rx="2" /><path d="M19 20h18m-6-6 6 6-6 6" /></>,
    copy: <><rect x="4" y="4" width="20" height="25" rx="2" /><rect x="16" y="11" width="20" height="25" rx="2" /><path d="M20 23h12m-4-4 4 4-4 4" /></>,
    duplex: <><rect x="12" y="9" width="16" height="22" rx="2" /><path d="M2 16h8m-3-3 3 3-3 3M38 24h-8m3-3-3 3 3 3" /></>,
    device: <><rect x="5" y="5" width="30" height="30" rx="3" /><path d="M5 13h30M13 13v22m8-16h8m-8 7h8" /></>
};

export default function AudioFlowIcon({ kind = 'device', className = '' }) {
    return <svg className={`audio-flow-icon ${className}`.trim()} viewBox="0 0 40 40" aria-hidden="true"
        fill="none" stroke="currentColor" strokeWidth="2.6" strokeLinecap="round" strokeLinejoin="round">
        {paths[kind] || paths.device}
    </svg>;
}
