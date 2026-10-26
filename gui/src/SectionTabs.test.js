import React, { act, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { Simulate } from 'react-dom/test-utils';
import SectionTabs from './SectionTabs';

test('subject tabs link their panels and move keyboard focus across enabled subjects', async () => {
    globalThis.IS_REACT_ACT_ENVIRONMENT = true;
    const container = document.createElement('div'); document.body.appendChild(container);
    const root = createRoot(container); const changed = jest.fn();
    function Example() {
        const [value, setValue] = useState('timeline');
        return <SectionTabs id="analysis" label="Analysis subjects" value={value} onChange={next => { changed(next); setValue(next); }}
            tabs={[{ id: 'timeline', label: 'Timeline' }, { id: 'unavailable', label: 'Unavailable', disabled: true }, { id: 'channels', label: 'Channels' }]} />;
    }
    try {
        await act(async () => root.render(<Example />));
        const first = container.querySelector('#analysis-tab-timeline');
        const last = container.querySelector('#analysis-tab-channels');
        expect(first.getAttribute('aria-controls')).toBe('analysis-panel-timeline');
        expect(first.tabIndex).toBe(0);
        first.focus();
        await act(async () => Simulate.keyDown(first, { key: 'ArrowRight' }));
        expect(changed).toHaveBeenLastCalledWith('channels');
        expect(document.activeElement).toBe(last);
        expect(last.getAttribute('aria-selected')).toBe('true');
        expect(first.tabIndex).toBe(-1);
        await act(async () => Simulate.keyDown(last, { key: 'ArrowRight' }));
        expect(document.activeElement).toBe(first);
        await act(async () => Simulate.keyDown(first, { key: 'End' }));
        expect(document.activeElement).toBe(last);
        await act(async () => Simulate.keyDown(last, { key: 'Home' }));
        expect(document.activeElement).toBe(first);
        await act(async () => Simulate.click(last));
        expect(last.getAttribute('aria-selected')).toBe('true');
    } finally { await act(async () => root.unmount()); container.remove(); }
});
