import React, { useRef } from 'react';
import './SectionTabs.css';

// Keep panels mounted in the caller so changing subjects preserves drafts.
export default function SectionTabs({ id, label, tabs, value, onChange }) {
    const listRef = useRef(null);
    const enabled = tabs.filter(tab => !tab.disabled);
    const selected = enabled.some(tab => tab.id === value) ? value : enabled[0]?.id;
    const move = (event, current) => {
        if (!['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(event.key) || !enabled.length) return;
        event.preventDefault();
        const index = enabled.findIndex(tab => tab.id === current);
        const next = event.key === 'Home' ? enabled[0] : event.key === 'End' ? enabled[enabled.length - 1]
            : enabled[(index + (event.key === 'ArrowRight' ? 1 : -1) + enabled.length) % enabled.length];
        onChange(next.id);
        [...listRef.current.querySelectorAll('[role="tab"]')].find(button => button.id === `${id}-tab-${next.id}`)?.focus();
    };
    return <div className="section-tabs" role="tablist" aria-label={label} ref={listRef}>
        {tabs.map(tab => <button type="button" role="tab" key={tab.id} id={`${id}-tab-${tab.id}`}
            aria-controls={`${id}-panel-${tab.id}`} aria-selected={selected === tab.id}
            tabIndex={selected === tab.id ? 0 : -1} disabled={tab.disabled}
            onClick={() => onChange(tab.id)} onKeyDown={event => move(event, tab.id)}>{tab.label}</button>)}
    </div>;
}
