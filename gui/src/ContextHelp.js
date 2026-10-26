import React from 'react';
import './ContextHelp.css';

export default function ContextHelp({ topic, onOpen, label }) {
    const accessibleLabel = label || `Help: ${topic.replace(/-/g, ' ')}`;
    return <button className="context-help" type="button" aria-label={accessibleLabel}
        title={accessibleLabel} aria-haspopup="dialog" onClick={event => {
            event.stopPropagation();
            onOpen(topic);
        }}><span aria-hidden="true">?</span></button>;
}
