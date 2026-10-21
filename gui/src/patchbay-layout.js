// Arrange hardware inputs, applications and hardware outputs in separate columns.
export const arrangePatchbayNodes = nodes => {
    const widths = [320, 320, 320];
    nodes.forEach(node => {
        const column = node.data.layoutColumn;
        widths[column] = Math.max(widths[column], node.measured?.width || 0);
    });
    const x = [30, 30 + widths[0] + 96, 30 + widths[0] + widths[1] + 192];
    const y = [30, 30, 30];
    return nodes.map(node => {
        const column = node.data.layoutColumn;
        const position = { x: x[column], y: y[column] };
        y[column] += (node.measured?.height || node.data.layoutHeight || 150) + 36;
        return { ...node, position };
    });
};
