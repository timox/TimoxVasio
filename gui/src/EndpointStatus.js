import React from 'react';

export default function EndpointStatus({ active }) {
    return <span className={`endpoint-status ${active ? 'active' : 'offline'}`} role="status"
        aria-label={active ? 'Endpoint active' : 'Endpoint offline'}>
        <svg viewBox="0 0 20 20" aria-hidden="true" focusable="false">
            <circle cx="10" cy="10" r="8" />
            {active ? <path d="m6 10 2.5 2.5L14 7" /> : <path d="M6 10h8" />}
        </svg>
        <span>{active ? 'Active' : 'Offline'}</span>
    </span>;
}
