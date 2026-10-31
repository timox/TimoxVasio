export const processExecutableName = imagePath => (imagePath || '').split(/[\\/]/).pop().toLowerCase();

export const defaultProcessCopyName = imagePath => `ProcessCopy+${processExecutableName(imagePath)}`;

export function processCopyTemplate(bridge) {
    return { id: bridge.id, name: bridge.name || defaultProcessCopyName(bridge.processImagePath),
        processName: processExecutableName(bridge.processImagePath), bufferDuration100ns: bridge.bufferDuration100ns };
}

export function processCopyCanConnect(bridge, runtime, newlySelected = false) {
    return bridge.enabled !== false && (runtime
        ? ['running', 'priming', 'starting'].includes(runtime.state) : newlySelected);
}
