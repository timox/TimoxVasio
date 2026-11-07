// jsdom does not implement the layout observer used by React Flow.
global.ResizeObserver = class ResizeObserver {
    observe() {}
    unobserve() {}
    disconnect() {}
};
