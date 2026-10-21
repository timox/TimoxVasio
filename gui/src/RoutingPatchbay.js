import React, { useRef, useState } from 'react';
import { createPortal } from 'react-dom';
import { ReactFlow, Background, Handle, Position, useNodesState, useReactFlow } from '@xyflow/react';
import '@xyflow/react/dist/style.css';
import { DEFAULT_ROUTE_COLOR, routeVisualKey } from './route-visuals';
import { transferProcessConnections } from './process-connection-transfer';
import { arrangePatchbayNodes } from './patchbay-layout';

const permitted = (sourceKind, destinationKind) =>
    sourceKind === 'physical-input' ? ['virtual-input', 'wasapi-render'].includes(destinationKind) :
        ['virtual-output', 'wasapi-capture'].includes(sourceKind) && ['physical-output', 'virtual-input', 'wasapi-render'].includes(destinationKind);
const emptyProfiles = [];

const withSearch = (groups, search) => {
    const query = search.trim().toLocaleLowerCase();
    return groups.map(group => ({ ...group, endpoints: group.endpoints.filter(endpoint =>
        !query || `${group.label} ${endpoint.name} ${endpoint.channel || ''}`.toLocaleLowerCase().includes(query)) }))
        .filter(group => !query || group.endpoints.length);
};

const PatchbayFlowNode = ({ data }) => data.render();
const flowNodeTypes = { patchbay: PatchbayFlowNode };
const PatchbayZoom = ({ visibleKey, onAutoArrange, empty }) => {
    const { zoomIn, zoomOut, fitView } = useReactFlow();
    const previousKey = useRef(visibleKey);
    React.useEffect(() => {
        if (previousKey.current === visibleKey) return undefined;
        previousKey.current = visibleKey;
        if (!visibleKey) return undefined;
        const timer = window.setTimeout(() => fitView({ padding: 0.15, maxZoom: 1, duration: 0 }), 50);
        return () => window.clearTimeout(timer);
    }, [visibleKey, fitView]);
    return <div className="patchbay-flow-controls" role="group" aria-label="Graph zoom">
        <button type="button" aria-label="Zoom out" onClick={() => zoomOut()}>−</button>
        <button type="button" aria-label="Zoom in" onClick={() => zoomIn()}>+</button>
        <button type="button" onClick={() => fitView({ padding: 0.15, maxZoom: 1, duration: 0 })}>Recenter graph</button>
        <button type="button" disabled={empty} onClick={onAutoArrange}
            title="Arrange blocks automatically without changing connections or names">Auto-arrange</button>
    </div>;
};
const PatchbayCanvas = ({ modelNodes, edges, visibleKey, layoutResetVersion, onSavePosition, onSaveLayout,
    disabled, validConnection, onConnect, onReconnect, onEdgeClick, onPaneContextMenu,
    onNodeContextMenu, onEdgeContextMenu }) => {
    const [nodes, setNodes, onNodesChange] = useNodesState(modelNodes);
    const previousResetVersion = useRef(layoutResetVersion);
    React.useEffect(() => {
        const resetPositions = previousResetVersion.current !== layoutResetVersion;
        previousResetVersion.current = layoutResetVersion;
        setNodes(current => {
            const existing = new Map(current.map(node => [node.id, node]));
            return modelNodes.map(node => {
                const old = existing.get(node.id);
                return old ? { ...old, ...node,
                    position: resetPositions ? node.position : old.position,
                    measured: old.measured } : node;
            });
        });
    }, [modelNodes, layoutResetVersion, setNodes]);
    return <ReactFlow nodes={nodes} edges={edges} nodeTypes={flowNodeTypes}
        fitView fitViewOptions={{ padding: 0.15, maxZoom: 1 }} minZoom={0.45} maxZoom={2}
        panOnDrag zoomOnScroll={false} zoomOnPinch zoomOnDoubleClick={false}
        deleteKeyCode={null} selectionKeyCode={null} nodesConnectable={!disabled}
        edgesReconnectable={!disabled} isValidConnection={validConnection}
        onNodesChange={onNodesChange}
        onNodeDragStop={(event, node) => onSavePosition(node.id, node.position)}
        onConnect={onConnect} onReconnect={onReconnect}
        onEdgeClick={(event, edge) => onEdgeClick(edge.id)}
        onPaneContextMenu={event => { event.preventDefault(); onPaneContextMenu(event); }}
        onNodeContextMenu={(event, node) => { event.preventDefault(); onNodeContextMenu(event, node.id); }}
        onEdgeContextMenu={(event, edge) => { event.preventDefault(); onEdgeContextMenu(event, edge.id); }}>
        <Background color="#dbe5ea" gap={20} />
        <PatchbayZoom visibleKey={visibleKey ? `${visibleKey}:${layoutResetVersion}` : ''} empty={!nodes.length}
            onAutoArrange={() => {
                const arranged = arrangePatchbayNodes(nodes);
                setNodes(arranged);
                onSaveLayout(arranged);
            }} />
    </ReactFlow>;
};

const processName = group => group.label.split(' · ')[0].trim().toLocaleLowerCase();
const shortPortName = endpoint => endpoint.name.replace(/^TimoxVasio .+? (In|Out) (\d+)/, '$1 $2');
const runtimeKey = group => group.id.endsWith(':input') || group.id.endsWith(':output')
    ? group.id.replace(/:(input|output)$/, '') : group.id;
const graphNodes = (sources, destinations, sourceGroups, destinationGroups) => {
    const all = [...sourceGroups, ...destinationGroups];
    const runtimesByName = new Map();
    all.filter(group => group.kind.startsWith('virtual') &&
        !group.id.startsWith('profile:') && !group.id.startsWith('saved:')).forEach(group => {
        const name = processName(group);
        if (!runtimesByName.has(name)) runtimesByName.set(name, new Set());
        runtimesByName.get(name).add(runtimeKey(group));
    });
    const nodeKey = group => {
        if (group.bridgeId) return `wasapi:${group.bridgeId}`;
        if (group.id.startsWith('profile:') || group.id.startsWith('saved:')) {
            const name = processName(group);
            const runtimes = runtimesByName.get(name);
            return runtimes?.size === 1 ? `process:${[...runtimes][0]}` :
                `${group.id.startsWith('profile:') ? 'profile' : 'saved'}:${name}`;
        }
        return group.kind.startsWith('virtual') ? `process:${runtimeKey(group)}` : `physical:${group.id}`;
    };
    const nodes = new Map();
    const add = (group, side) => {
        const key = nodeKey(group);
        if (!nodes.has(key)) nodes.set(key, { key, label: group.label, type: group.id.startsWith('profile:') ? 'profile' :
            group.id.startsWith('saved:') ? 'saved' :
            group.bridgeId ? 'wasapi' : group.kind.startsWith('physical') ? 'physical' : 'process', bridgeId: group.bridgeId, inputs: [], outputs: [] });
        const node = nodes.get(key);
        if ((node.type === 'profile' || node.type === 'saved') && !group.id.startsWith('profile:') && !group.id.startsWith('saved:')) {
            node.type = 'process'; node.label = group.label;
        }
        node[side === 'source' ? 'outputs' : 'inputs'].push(group);
    };
    sources.forEach(group => add(group, 'source'));
    destinations.forEach(group => add(group, 'destination'));
    return [...nodes.values()];
};

const layoutStorageKey = 'timoxvasio.patchbay.layout.v1';
const readNodeLayout = () => {
    try { return JSON.parse(window.localStorage.getItem(layoutStorageKey)) || {}; }
    catch { return {}; }
};

export default function RoutingPatchbay({ sourceGroups = [], destinationGroups = [], routes = [], onToggleRoute,
    onReconnectRoute = () => {},
    selectedRouteId = null, onSelectRoute = () => {}, routeVisuals = {}, onChangeRouteVisual = () => {},
    onBulkChangeRouteVisuals = () => {}, onEditRoute = () => {}, confirmedRoutes = [], activeRoutes = [],
    meters = {}, disabled = false, profiles = emptyProfiles, onAssignProfile = async () => {},
    onRemoveProcessConfiguration = async () => {}, onTransferProcessConnections = () => {},
    onCreateConnection = () => {}, onShowProcessDetail = () => {},
    onCreateBridge, onEditBridge = () => {}, onRemoveBridge = () => {}, isConnectionAllowed = () => true }) {
    const [contextMenu, setContextMenu] = useState(null);
    const [processDialogKey, setProcessDialogKey] = useState(null);
    React.useEffect(() => {
        if (!processDialogKey) return undefined;
        const closeOnEscape = event => { if (event.key === 'Escape') setProcessDialogKey(null); };
        document.addEventListener('keydown', closeOnEscape);
        return () => document.removeEventListener('keydown', closeOnEscape);
    }, [processDialogKey]);
    const [selectedSourceId, setSelectedSourceId] = useState(null);
    const connectedOnly = true;
    const [focusPairId, setFocusPairId] = useState('');
    const [matrixOpen, setMatrixOpen] = useState(false);
    const matrixCloseRef = useRef(null);
    const matrixTriggerRef = useRef(null);
    const graphRef = useRef(null);
    const [nodeLayout, setNodeLayout] = useState(readNodeLayout);
    const [layoutResetVersion, setLayoutResetVersion] = useState(0);
    const [editingNodeKey, setEditingNodeKey] = useState(null);
    const [renameDraft, setRenameDraft] = useState('');
    const [selectedNodeKey, setSelectedNodeKey] = useState(null);
    const [connectionDialogOpen, setConnectionDialogOpen] = useState(false);
    React.useEffect(() => { if (!selectedRouteId) setConnectionDialogOpen(false); }, [selectedRouteId]);
    const [profileDraft, setProfileDraft] = useState({ inputChannels: 2, outputChannels: 2 });
    const [processBusy, setProcessBusy] = useState(false);
    const [processNotice, setProcessNotice] = useState('');
    const [confirmProcessRemoval, setConfirmProcessRemoval] = useState(false);
    const [transferTargetName, setTransferTargetName] = useState('');
    const [sourceSearch, setSourceSearch] = useState('');
    const [destinationSearch, setDestinationSearch] = useState('');
    const [collapsedGroups, setCollapsedGroups] = useState({});
    const [bulkOpen, setBulkOpen] = useState(false);
    const [bulkSearch, setBulkSearch] = useState('');
    const [bulkSelectedKeys, setBulkSelectedKeys] = useState([]);
    const [bulkLabel, setBulkLabel] = useState('');
    const [bulkColor, setBulkColor] = useState(DEFAULT_ROUTE_COLOR);
    const [bulkUseLabel, setBulkUseLabel] = useState(false);
    const [bulkUseColor, setBulkUseColor] = useState(false);
    const [bulkNotice, setBulkNotice] = useState('');
    React.useEffect(() => {
        if (!selectedNodeKey && !connectionDialogOpen && !bulkOpen) return undefined;
        const closeOnEscape = event => {
            if (event.key !== 'Escape' || matrixOpen) return;
            setSelectedNodeKey(null); setConnectionDialogOpen(false); setBulkOpen(false);
        };
        document.addEventListener('keydown', closeOnEscape);
        return () => document.removeEventListener('keydown', closeOnEscape);
    }, [selectedNodeKey, connectionDialogOpen, bulkOpen, matrixOpen]);
    const selectedSourceRef = useRef(null);
    const selectSource = id => { selectedSourceRef.current = id; setSelectedSourceId(id); };
    const layoutKey = (group, side) => group.kind.startsWith('virtual')
        ? `${side}:app:${processName(group)}` : `${side}:${group.id}`;
    React.useEffect(() => {
        try { window.localStorage.setItem(layoutStorageKey, JSON.stringify(nodeLayout)); }
        catch { /* Local storage may be disabled. */ }
    }, [nodeLayout]);
    const sourceGroupByEndpoint = new Map(sourceGroups.flatMap(group => group.endpoints.map(endpoint => [endpoint.id, group])));
    const destinationGroupByEndpoint = new Map(destinationGroups.flatMap(group => group.endpoints.map(endpoint => [endpoint.id, group])));
    const connectionPairs = [...new Map(routes.map(route => {
        const source = sourceGroupByEndpoint.get(route.sourceEndpointId);
        const destination = destinationGroupByEndpoint.get(route.destinationEndpointId);
        if (!source || !destination) return null;
        const id = `${source.id}|${destination.id}`;
        return [id, { id, label: `${source.label} → ${destination.label}` }];
    }).filter(Boolean)).values()];
    const effectiveFocusPairId = connectionPairs.some(pair => pair.id === focusPairId) ? focusPairId : '';
    const confirmedByPair = new Map(confirmedRoutes.map(route => [`${route.sourceEndpointId}|${route.destinationEndpointId}`, route]));
    const activePairs = new Set(activeRoutes.map(route => `${route.sourceEndpointId}|${route.destinationEndpointId}`));
    const activeRouteIds = new Set(activeRoutes.map(route => route.id));
    const routeState = route => {
        const key = `${route.sourceEndpointId}|${route.destinationEndpointId}`;
        const saved = confirmedByPair.get(key);
        return !saved || saved.mute !== route.mute || saved.gainDb !== route.gainDb
            ? 'pending' : activeRouteIds.has(route.id) || activePairs.has(key) ? 'active' : 'saved';
    };
    const focusedRoutes = connectedOnly && effectiveFocusPairId ? routes.filter(route => {
        const source = sourceGroupByEndpoint.get(route.sourceEndpointId);
        const destination = destinationGroupByEndpoint.get(route.destinationEndpointId);
        return `${source?.id}|${destination?.id}` === effectiveFocusPairId;
    }) : routes;
    const linkedSources = new Set(focusedRoutes.map(route => route.sourceEndpointId));
    const linkedDestinations = new Set(focusedRoutes.map(route => route.destinationEndpointId));
    const allLinkedDestinations = new Set(routes.map(route => route.destinationEndpointId));
    const focusGroups = (groups, linked) => connectedOnly ? groups.map(group => ({ ...group,
        endpoints: group.endpoints.filter(endpoint => linked.has(endpoint.id)) })) : groups;
    const sources = withSearch(focusGroups(sourceGroups, linkedSources), sourceSearch);
    const destinations = withSearch(focusGroups(destinationGroups, linkedDestinations), destinationSearch);
    const nodes = graphNodes(sources, destinations, sourceGroups, destinationGroups);
    const allNodes = graphNodes(sourceGroups, destinationGroups, sourceGroups, destinationGroups);
    const selectedNode = allNodes.find(node => node.key === selectedNodeKey);
    const selectedProcessName = selectedNode?.label.split(' · ')[0].trim();
    const assignedProfile = profiles.find(profile => profile.processName.toLocaleLowerCase() === selectedProcessName?.toLocaleLowerCase());
    const includesProfile = !!assignedProfile;
    const runtimePrefix = selectedNode?.key.startsWith('process:') ? `virtual:${selectedNode.key.slice(8)}:` : null;
    const profilePrefix = selectedProcessName ? `virtual:timoxvasio:app:${selectedProcessName.toLocaleLowerCase()}:` : null;
    const processRoutes = selectedNode ? routes.filter(route => [route.sourceEndpointId, route.destinationEndpointId]
        .some(id => (runtimePrefix && id.startsWith(runtimePrefix)) ||
            (profilePrefix && id.toLocaleLowerCase().startsWith(profilePrefix)))) : [];
    const transferTargets = selectedNode?.type === 'saved' ? allNodes.filter(node => node.type === 'process' &&
        processName({ label: node.label }) !== selectedProcessName?.toLocaleLowerCase()) : [];
    const transferPreview = transferTargetName && selectedProcessName
        ? transferProcessConnections(routes, selectedProcessName, transferTargetName) : null;
    React.useEffect(() => {
        if (!selectedNodeKey) return;
        setTransferTargetName('');
        const profile = profiles.find(item => item.processName.toLocaleLowerCase() === selectedProcessName?.toLocaleLowerCase());
        setProfileDraft({ inputChannels: profile?.inputChannels || 2, outputChannels: profile?.outputChannels || 2 });
    }, [selectedNodeKey, selectedProcessName, profiles]);
    const nodeKeyByGroup = new Map(allNodes.flatMap(node => [
        ...node.outputs.map(group => [`source:${group.id}`, node.key]),
        ...node.inputs.map(group => [`destination:${group.id}`, node.key])
    ]));
    const groupKey = (side, group) => nodeKeyByGroup.get(`${side}:${group.id}`) || `${side}:${group.id}`;
    const isCollapsed = (group, side) => {
        const node = allNodes.find(item => item.key === groupKey(side, group));
        return !sourceSearch.trim() && !destinationSearch.trim() &&
            (collapsedGroups[groupKey(side, group)] ??
                (!connectedOnly && (node?.inputs.concat(node.outputs).reduce((count, item) => count + item.endpoints.length, 0) || group.endpoints.length) > 8));
    };
    const visibleSourceIds = new Set(sources.filter(group => !isCollapsed(group, 'source'))
        .flatMap(group => group.endpoints.map(endpoint => endpoint.id)));
    const visibleDestinationIds = new Set(destinations.filter(group => !isCollapsed(group, 'destination'))
        .flatMap(group => group.endpoints.map(endpoint => endpoint.id)));
    const sourceMap = new Map(sourceGroups.flatMap(group => group.endpoints.map(endpoint => [endpoint.id, group.kind])));
    const selectedKind = sourceMap.get(selectedSourceId);
    const selectedRoute = routes.find(route => route.id === selectedRouteId);
    const matrixSource = selectedRoute ? sourceGroupByEndpoint.get(selectedRoute.sourceEndpointId) : null;
    const matrixDestination = selectedRoute ? destinationGroupByEndpoint.get(selectedRoute.destinationEndpointId) : null;
    const matrixRoutes = matrixSource && matrixDestination ? routes.filter(route =>
        sourceGroupByEndpoint.get(route.sourceEndpointId)?.id === matrixSource.id &&
        destinationGroupByEndpoint.get(route.destinationEndpointId)?.id === matrixDestination.id) : [];
    React.useEffect(() => {
        if (!matrixOpen) return undefined;
        const trigger = matrixTriggerRef.current || graphRef.current;
        matrixCloseRef.current?.focus();
        const handleKeyDown = event => {
            if (event.key === 'Escape') setMatrixOpen(false);
            if (event.key === 'Tab') {
                event.preventDefault();
                matrixCloseRef.current?.focus();
            }
        };
        document.addEventListener('keydown', handleKeyDown);
        return () => {
            document.removeEventListener('keydown', handleKeyDown);
            trigger?.focus();
        };
    }, [matrixOpen]);
    const selectedVisual = selectedRoute && routeVisuals[routeVisualKey(selectedRoute)];
    const endpointNames = new Map([...sourceGroups, ...destinationGroups]
        .flatMap(group => group.endpoints.map(endpoint => [endpoint.id, endpoint.name])));
    const describeRoute = route => `${routeVisuals[routeVisualKey(route)]?.label ? `${routeVisuals[routeVisualKey(route)].label} · ` : ''}${endpointNames.get(route.sourceEndpointId) || route.sourceEndpointId} → ${endpointNames.get(route.destinationEndpointId) || route.destinationEndpointId}`;
    const bulkQuery = bulkSearch.trim().toLocaleLowerCase();
    const bulkVisibleRoutes = routes.filter(route => !bulkQuery || describeRoute(route).toLocaleLowerCase().includes(bulkQuery));
    const bulkSelectedRoutes = routes.filter(route => bulkSelectedKeys.includes(routeVisualKey(route)));
    const toggleBulkRoute = (key, selected) => setBulkSelectedKeys(current => selected
        ? [...new Set([...current, key])] : current.filter(item => item !== key));
    const routeVisible = route => visibleSourceIds.has(route.sourceEndpointId) && visibleDestinationIds.has(route.destinationEndpointId);
    const selectedRouteVisible = !!(selectedRoute && routeVisible(selectedRoute));
    const hiddenRouteCount = routes.filter(route => !routeVisible(route)).length;
    const openContextMenu = (event, kind, id = null) => {
        setContextMenu({ kind, id, x: Math.min(event.clientX, window.innerWidth - 250),
            y: Math.min(event.clientY, window.innerHeight - 250) });
    };
    React.useEffect(() => {
        if (!contextMenu) return undefined;
        const dismiss = event => {
            if (event.type === 'keydown' && event.key !== 'Escape') return;
            setContextMenu(null);
        };
        document.addEventListener('pointerdown', dismiss);
        document.addEventListener('keydown', dismiss);
        return () => {
            document.removeEventListener('pointerdown', dismiss);
            document.removeEventListener('keydown', dismiss);
        };
    }, [contextMenu]);
    const contextNode = contextMenu?.kind === 'node' ? allNodes.find(node => node.key === contextMenu.id) : null;
    const contextRoute = contextMenu?.kind === 'edge' ? routes.find(route => route.id === contextMenu.id) : null;
    const routesForNode = node => routes.filter(route => {
        const ids = [...node.inputs, ...node.outputs].flatMap(group => group.endpoints.map(endpoint => endpoint.id));
        const appPrefix = `virtual:timoxvasio:app:${node.label.split(' · ')[0].trim().toLocaleLowerCase()}:`;
        return [route.sourceEndpointId, route.destinationEndpointId].some(id =>
            ids.includes(id) || (node.type !== 'physical' && id.toLocaleLowerCase().startsWith(appPrefix)));
    });
    const contextNodeRoutes = contextNode ? routesForNode(contextNode) : [];
    const processDialogNode = allNodes.find(node => node.key === processDialogKey);
    const processDialogRoutes = processDialogNode ? routesForNode(processDialogNode) : [];
    const menuAction = callback => { setContextMenu(null); callback(); };

    const setAllGroupsCollapsed = collapsed => {
        setCollapsedGroups(Object.fromEntries([
            ...sourceGroups.map(group => [groupKey('source', group), collapsed]),
            ...destinationGroups.map(group => [groupKey('destination', group), collapsed])
        ]));
        if (collapsed) selectSource(null);
    };

    const showConnectionView = () => {
        setFocusPairId('');
        setSourceSearch(''); setDestinationSearch('');
        setAllGroupsCollapsed(false);
        selectSource(null);
    };

    const revealRoute = (route, focus = false) => {
        if (!route) return;
        const sourceGroup = sourceGroups.find(group => group.endpoints.some(endpoint => endpoint.id === route.sourceEndpointId));
        const destinationGroup = destinationGroups.find(group => group.endpoints.some(endpoint => endpoint.id === route.destinationEndpointId));
        if (!sourceGroup || !destinationGroup) return;
        onSelectRoute(route.id);
        setSourceSearch(focus ? sourceGroup.endpoints.find(endpoint => endpoint.id === route.sourceEndpointId).name : '');
        setDestinationSearch(focus ? destinationGroup.endpoints.find(endpoint => endpoint.id === route.destinationEndpointId).name : '');
        setCollapsedGroups(current => ({ ...current,
            [groupKey('source', sourceGroup)]: false,
            [groupKey('destination', destinationGroup)]: false
        }));
    };
    const revealSelectedRoute = () => revealRoute(selectedRoute);

    const connect = (destinationId, destinationKind) => {
        const sourceId = selectedSourceRef.current;
        if (disabled || !sourceId || !permitted(sourceMap.get(sourceId), destinationKind) || !isConnectionAllowed(sourceId, destinationId)) return;
        onToggleRoute(sourceId, destinationId);
        selectSource(null);
    };

    const renderPorts = (groups, side) => groups.map(group => <div className="patchbay-port-group" key={group.id}>
        {groups.length > 1 && <small>{group.id.startsWith('profile:') ? 'Channel profile' :
            group.id.startsWith('saved:') ? 'Configured ports' : 'Open ports'}</small>}
        <ul>{group.endpoints.map(endpoint => {
            const connected = side === 'destination' && allLinkedDestinations.has(endpoint.id);
            const invalid = side === 'destination' && selectedSourceId &&
                (!permitted(selectedKind, group.kind) || !isConnectionAllowed(selectedSourceId, endpoint.id));
            return <li key={endpoint.id}>
                <button type="button" className={`patchbay-port nodrag ${side}${selectedSourceId === endpoint.id ? ' selected' : ''}${connected ? ' linked' : ''}${selectedRoute && (side === 'source' ? selectedRoute.sourceEndpointId : selectedRoute.destinationEndpointId) === endpoint.id ? ' route-selected' : ''}`}
                    data-port-id={endpoint.id} data-side={side} disabled={disabled || !!invalid}
                    aria-pressed={side === 'source' ? selectedSourceId === endpoint.id : undefined}
                    aria-label={side === 'source' ? `${group.label}, ${endpoint.name}, select as source` :
                        `${group.label}, ${endpoint.name}${connected ? ', connected' : ''}${selectedSourceId ? ', select as destination' : ''}`}
                    title={endpoint.name}
                    onClick={() => { if (side === 'source') selectSource(endpoint.id);
                        else connect(endpoint.id, group.kind); }}>
                    {side === 'destination' && <Handle type="target" id={endpoint.id} position={Position.Left}
                        isConnectable={!disabled} className={`patchbay-flow-handle ${group.kind}`} />}
                    <span>{shortPortName(endpoint)}</span>
                    {meters[endpoint.id]?.peakDbfs != null && <small className="patchbay-level" title="Measured peak">{meters[endpoint.id].peakDbfs.toFixed(0)} dBFS</small>}
                    {side === 'source' && <Handle type="source" id={endpoint.id} position={Position.Right}
                        isConnectable={!disabled} className={`patchbay-flow-handle ${group.kind}`} />}
                </button>
            </li>;
        })}</ul>
    </div>);

    const renderNode = node => {
        const groups = [...node.inputs, ...node.outputs];
        const collapsed = groups.length > 0 && isCollapsed(groups[0], node.inputs.length ? 'destination' : 'source');
        const nodeKey = node.key;
        const legacyGroup = node.outputs[0] || node.inputs[0];
        const legacySide = node.outputs.length ? 'source' : 'destination';
        const layout = nodeLayout[nodeKey] || nodeLayout[layoutKey(legacyGroup, legacySide)] || {};
        const endpointIds = new Set(groups.flatMap(group => group.endpoints.map(endpoint => endpoint.id)));
        const linkCount = focusedRoutes.filter(route => endpointIds.has(route.sourceEndpointId) || endpointIds.has(route.destinationEndpointId)).length;
        const fullNode = allNodes.find(item => item.key === node.key) || node;
        const profile = profiles.find(item => item.processName.toLocaleLowerCase() === processName(fullNode));
        const capacity = (side, field, routeField) => {
            const endpoints = fullNode[side].flatMap(group => group.endpoints);
            const channelKey = endpoint => endpoint.id.match(/:(\d+)$/)?.[1] || endpoint.id;
            const linked = new Set(routes.map(route => route[routeField]));
            const used = new Set(endpoints.filter(endpoint => linked.has(endpoint.id)).map(channelKey)).size;
            const available = Math.max(profile?.[field] || 0,
                ...endpoints.map(endpoint => Number(endpoint.id.match(/:(\d+)$/)?.[1]) || 0),
                new Set(endpoints.map(channelKey)).size);
            return { used, available };
        };
        const inputs = capacity('inputs', 'inputChannels', 'destinationEndpointId');
        const outputs = capacity('outputs', 'outputChannels', 'sourceEndpointId');
        return <section className={`patchbay-interface ${node.type}`} data-node-key={nodeKey}>
        <div className="patchbay-interface-title"><div className="patchbay-group-toggle">
            <button type="button" className="patchbay-disclosure-button nodrag" aria-label={`${collapsed ? 'Expand' : 'Collapse'} ${layout.label || node.label}`}
            data-group-key={nodeKey} aria-expanded={!collapsed}
            disabled={!!(sourceSearch.trim() || destinationSearch.trim())}
            onClick={() => {
                setCollapsedGroups(current => ({ ...current, [nodeKey]: !collapsed }));
                if (!collapsed && endpointIds.has(selectedSourceId)) selectSource(null);
            }}>{collapsed ? '▸' : '▾'}</button>
            <span className={`patchbay-object-icon ${node.type}`} aria-hidden="true">{node.type === 'wasapi' ? '◈' : node.type === 'physical' ? '▣' :
                node.type === 'profile' ? '◇' : node.type === 'saved' ? '◌' : '▤'}</span>
            <strong title={layout.label || node.label}>{layout.label || node.label}</strong>
            <span>{linkCount} {linkCount === 1 ? 'link' : 'links'}</span>
        </div>
        </div>
        <div className="patchbay-capacity" title="Connected channels / available channel capacity. Independent of graph filters.">
            In {inputs.used}/{inputs.available} · Out {outputs.used}/{outputs.available} used
        </div>
        {editingNodeKey === nodeKey && createPortal(<div className="routing-dialog-backdrop"><div className="routing-dialog" role="dialog" aria-modal="true" aria-label="Rename block">
        <h2>Rename block</h2><form className="patchbay-node-editor" onSubmit={event => {
            event.preventDefault();
            setNodeLayout(current => ({ ...current, [nodeKey]: { ...current[nodeKey], label: renameDraft.trim().slice(0, 60) } }));
            setEditingNodeKey(null);
        }}>
            <input autoFocus className="nodrag" aria-label={`Display name for ${node.label}`} maxLength="60" value={renameDraft}
                onChange={event => setRenameDraft(event.target.value)} />
            <button type="submit">Save</button><button type="button" onClick={() => setEditingNodeKey(null)}>Cancel</button>
        </form></div></div>, document.body)}
        {!collapsed && <div className={`patchbay-node-ports${node.inputs.length && node.outputs.length ? '' : ' single-sided'}`}>
            {node.inputs.length > 0 && <div className="patchbay-node-inputs"><span className="patchbay-port-heading">Inputs</span>{renderPorts(node.inputs, 'destination')}</div>}
            {node.outputs.length > 0 && <div className="patchbay-node-outputs"><span className="patchbay-port-heading">Outputs</span>{renderPorts(node.outputs, 'source')}</div>}
        </div>}
    </section>;
    };

    const columnY = [30, 30, 30];
    const flowNodes = nodes.map(node => {
        const column = !['physical', 'wasapi'].includes(node.type) ? 1 : node.outputs.length ? 0 : 2;
        const groups = [...node.inputs, ...node.outputs];
        const portCount = groups.reduce((total, group) => total + group.endpoints.length, 0);
        const collapsed = groups.length && isCollapsed(groups[0], node.inputs.length ? 'destination' : 'source');
        const base = { x: [30, 445, 860][column], y: columnY[column] };
        columnY[column] += (collapsed ? 66 : 82 + portCount * 35) + 36;
        const legacyGroup = node.outputs[0] || node.inputs[0];
        const layout = nodeLayout[node.key] || nodeLayout[layoutKey(legacyGroup, node.outputs.length ? 'source' : 'destination')] || {};
        return { id: node.key, type: 'patchbay', position: { x: layout.x ?? base.x + (layout.dx || 0),
            y: layout.y ?? base.y + (layout.dy || 0) }, data: { render: () => renderNode(node),
                layoutColumn: column, layoutHeight: collapsed ? 66 : 82 + portCount * 35 },
            draggable: true, selectable: false };
    });
    const visibleNodeKeys = new Set(flowNodes.map(node => node.id));
    const visibleKey = flowNodes.map(node => node.id).sort().join('|') +
        `:${connectedOnly}:${effectiveFocusPairId}:${sourceSearch}:${destinationSearch}`;
    const flowEdges = routes.filter(route => routeVisible(route)).map(route => {
        const sourceGroup = sourceGroupByEndpoint.get(route.sourceEndpointId);
        const destinationGroup = destinationGroupByEndpoint.get(route.destinationEndpointId);
        const source = sourceGroup && groupKey('source', sourceGroup);
        const target = destinationGroup && groupKey('destination', destinationGroup);
        if (!visibleNodeKeys.has(source) || !visibleNodeKeys.has(target)) return null;
        const state = routeState(route);
        const selected = route.id === selectedRouteId;
        return { id: route.id, source, target, sourceHandle: route.sourceEndpointId,
            targetHandle: route.destinationEndpointId, type: 'default', interactionWidth: 28,
            reconnectable: !disabled, selected,
            className: `connection ${state}${route.mute ? ' muted' : ''}${selected ? ' selected' : selectedRouteId ? ' subdued' : ''}`,
            style: { stroke: routeVisuals[routeVisualKey(route)]?.color || DEFAULT_ROUTE_COLOR,
                strokeWidth: selected ? 4 : state === 'active' ? 2.8 : 2.2,
                strokeDasharray: route.mute ? '2 5' : state === 'pending' ? '7 4' : undefined },
            label: route.gainDb > 0 ? '▲' : undefined,
            ariaLabel: describeRoute(route) };
    }).filter(Boolean);
    const validFlowConnection = connection => {
        const sourceGroup = sourceGroupByEndpoint.get(connection.sourceHandle);
        const destinationGroup = destinationGroupByEndpoint.get(connection.targetHandle);
        return !!sourceGroup && !!destinationGroup && permitted(sourceGroup.kind, destinationGroup.kind) &&
            isConnectionAllowed(connection.sourceHandle, connection.targetHandle);
    };

    return <div className="patchbay" style={{ '--selected-route-color': selectedVisual?.color || DEFAULT_ROUTE_COLOR }}>
        <details className="patchbay-legend" aria-label="Graph symbols">
            <summary>Graph key</summary><div>
            <span><b className="physical">▣</b> Physical I/O</span><span><b className="process">▤</b> Process / virtual I/O</span>
            <span><b>◈</b> Windows audio bridge</span>
            <span><b className="profile">◇</b> Channel profile</span><span><b className="saved">◌</b> Process not open</span>
            <span><i className="line saved" /> Saved</span>
            <span><i className="line pending" /> Pending</span><span><i className="line muted" /> Muted</span>
            <span>▲ Gain above 0 dB</span>
            </div>
        </details>
        <div className="patchbay-toolbar">
            <p>{connectedOnly ? 'Saved connections and pending changes' :
                'Select an output, then an input. Apply configuration to activate the change.'}</p>
            <div className="patchbay-toolbar-actions">
                {connectedOnly && connectionPairs.length > 0 && <label className="patchbay-focus">Connection pair
                    <select value={effectiveFocusPairId} onChange={event => { setFocusPairId(event.target.value); setMatrixOpen(false); selectSource(null); }}>
                        <option value="">All saved connections</option>
                        {connectionPairs.map(pair => <option key={pair.id} value={pair.id}>{pair.label}</option>)}
                    </select></label>}
                <button type="button" title="Expand configured connections only" onClick={showConnectionView}>
                    Expand all
                </button>
                <details className="patchbay-tools"><summary>Graph tools</summary><div>
                {Object.keys(nodeLayout).length > 0 && <button type="button" onClick={() => {
                    setNodeLayout({}); setLayoutResetVersion(current => current + 1);
                }}>
                    Reset block layout and names
                </button>}
                {routes.length > 0 && <button type="button" onClick={() => revealRoute(selectedRoute || routes[0], true)}>
                    Show a configured connection
                </button>}
                <button type="button" onClick={() => setAllGroupsCollapsed(true)}>Collapse all</button>
                <button type="button" onClick={() => selectSource(null)} disabled={!selectedSourceId}>Cancel selection</button>
                </div></details>
            </div>
        </div>
        <div className="patchbay-search">
            <label>Search sources<input value={sourceSearch} onChange={event => setSourceSearch(event.target.value)} placeholder="Interface or channel" /></label>
            <label>Search destinations<input value={destinationSearch} onChange={event => setDestinationSearch(event.target.value)} placeholder="Interface or channel" /></label>
        </div>
        <div className="patchbay-workspace">
        <div className="patchbay-flow" role="region" aria-label="Graphical connections"
            ref={graphRef}
            onContextMenu={event => {
                if (event.defaultPrevented) return;
                event.preventDefault();
                openContextMenu(event, 'pane');
            }}
            tabIndex="0" onKeyDown={event => {
                if (event.key === 'Escape') { selectSource(null); setContextMenu(null); }
                if (event.key === 'ContextMenu' || (event.shiftKey && event.key === 'F10')) {
                    event.preventDefault(); setContextMenu({ kind: 'pane', x: 24, y: 140 });
                }
            }}>
            <PatchbayCanvas modelNodes={flowNodes} edges={flowEdges}
                visibleKey={flowNodes.length ? visibleKey : ''} layoutResetVersion={layoutResetVersion}
                disabled={disabled} validConnection={validFlowConnection}
                onSaveLayout={arranged => {
                    setNodeLayout(current => {
                        const next = { ...current };
                        arranged.forEach(node => {
                            const model = nodes.find(item => item.key === node.id);
                            if (!model) return;
                            const group = model.outputs[0] || model.inputs[0];
                            const previous = current[node.id] || current[layoutKey(group,
                                model.outputs.length ? 'source' : 'destination')] || {};
                            next[node.id] = { ...previous, ...node.position };
                        });
                        return next;
                    });
                    setLayoutResetVersion(current => current + 1);
                }}
                onSavePosition={(nodeId, position) => setNodeLayout(current => ({ ...current,
                    [nodeId]: { ...current[nodeId], x: position.x, y: position.y } }))}
                onConnect={connection => {
                    if (!disabled && validFlowConnection(connection))
                        onToggleRoute(connection.sourceHandle, connection.targetHandle);
                    selectSource(null);
                }}
                onReconnect={(edge, connection) => {
                    if (!disabled && validFlowConnection(connection))
                        onReconnectRoute(edge.id, connection.sourceHandle, connection.targetHandle);
                }}
                onEdgeClick={routeId => { setSelectedNodeKey(null); onSelectRoute(routeId); }}
                onPaneContextMenu={event => openContextMenu(event, 'pane')}
                onNodeContextMenu={(event, nodeId) => openContextMenu(event, 'node', nodeId)}
                onEdgeContextMenu={(event, routeId) => openContextMenu(event, 'edge', routeId)} />
            {!flowNodes.length && <div className="patchbay-flow-empty" role="status">
                <strong>No visible connections</strong>
                <span>{sourceSearch || destinationSearch ? 'The current search hides every port.' :
                    connectedOnly && routes.length ? 'This connection pair has no visible ports.' :
                        connectedOnly ? 'No connections are configured yet.' : 'No ports are currently available.'}</span>
                {(sourceSearch || destinationSearch) && <button type="button" onClick={() => {
                    setSourceSearch(''); setDestinationSearch('');
                }}>Clear searches</button>}

            </div>}
        </div>
        {(selectedNode || (selectedRoute && connectionDialogOpen) || bulkOpen) && createPortal(<div className="routing-dialog-backdrop" onMouseDown={event => {
            if (event.target === event.currentTarget) {
                setSelectedNodeKey(null); setConnectionDialogOpen(false); setBulkOpen(false);
            }
        }}><aside className="patchbay-inspector patchbay-inspector-dialog" role="dialog" aria-modal="true"
            aria-label={selectedNode ? 'Process configuration' : bulkOpen ? 'Group connection appearance' : 'Connection properties'}>
            <div className="routing-dialog-heading"><h2>{selectedNode ? 'Process configuration' : bulkOpen ? 'Group connection appearance' : 'Connection properties'}</h2>
                <button type="button" onClick={() => { setSelectedNodeKey(null); setConnectionDialogOpen(false); setBulkOpen(false); }}>Close</button></div>
            {selectedNode ? <div className="patchbay-process-editor">
                <h3>{selectedNode.type === 'profile' ? 'Channel profile' :
                    selectedNode.type === 'saved' ? 'Process not open' : 'Process configuration'}</h3>
                <strong>{selectedNode.label}</strong>
                <p className="hint">{selectedNode.type === 'profile' ? 'This channel profile is waiting for the application to open.' :
                    selectedNode.type === 'saved' ? 'These connections belong to this executable name. They will activate when that process opens the matching ASIO channels.' :
                    'Assign channel capacity to this process. The profile applies to this executable when it next opens ASIO.'}</p>
                {selectedNode.type === 'process' && <>
                    <label>Input channels<input aria-label="Process profile input channels" type="number" min="1" max="256"
                        value={profileDraft.inputChannels} onChange={event => setProfileDraft(current => ({ ...current,
                            inputChannels: Number(event.target.value) }))} /></label>
                    <label>Output channels<input aria-label="Process profile output channels" type="number" min="1" max="256"
                        value={profileDraft.outputChannels} onChange={event => setProfileDraft(current => ({ ...current,
                            outputChannels: Number(event.target.value) }))} /></label>
                    <button type="button" disabled={disabled || processBusy || !Number.isInteger(profileDraft.inputChannels) ||
                        !Number.isInteger(profileDraft.outputChannels) || profileDraft.inputChannels < 1 ||
                        profileDraft.outputChannels < 1 || profileDraft.inputChannels > 256 || profileDraft.outputChannels > 256}
                        onClick={async () => {
                            setProcessBusy(true); setProcessNotice('');
                            try { await onAssignProfile(selectedProcessName, profileDraft);
                                setProcessNotice('Profile saved. Restart the application to use the new channel capacity.'); }
                            catch (error) { setProcessNotice(error.message); }
                            finally { setProcessBusy(false); }
                        }}>{assignedProfile ? 'Update assigned profile' : 'Assign profile to this process'}</button>
                </>}
                <p className="hint">{processRoutes.length} saved {processRoutes.length === 1 ? 'connection' : 'connections'} belong to this executable.</p>
                {selectedNode.type === 'saved' && processRoutes.length > 0 && transferTargets.length > 0 &&
                    <div className="process-connection-transfer">
                        <label>Move saved connections to an open process
                            <select value={transferTargetName} onChange={event => setTransferTargetName(event.target.value)}>
                                <option value="">Choose a process</option>
                                {transferTargets.map(node => <option key={node.key} value={node.label.split(' · ')[0].trim()}>{node.label}</option>)}
                            </select>
                        </label>
                        <button type="button" disabled={disabled || !transferTargetName}
                            onClick={() => {
                                onTransferProcessConnections(selectedProcessName, transferTargetName);
                                setSelectedNodeKey(null);
                            }}>Move {processRoutes.length} connections to draft</button>
                        {transferPreview && <p className="hint">{transferPreview.moved - transferPreview.duplicates} new links; {transferPreview.duplicates} already exist on the target and keep their current settings.</p>}
                        <p className="hint">The old executable name is replaced in these connections. Review the draft before applying it.</p>
                    </div>}
                {(processRoutes.length > 0 || (includesProfile && assignedProfile)) && <>
                    <button type="button" className="remove-process-configuration" disabled={disabled || processBusy}
                        onClick={() => setConfirmProcessRemoval(true)}>Remove this configuration…</button>
                    {confirmProcessRemoval && <div className="process-remove-confirm" role="alert">
                        <p>Remove {processRoutes.length} saved {processRoutes.length === 1 ? 'connection' : 'connections'}{includesProfile && assignedProfile ? ' and the assigned profile' : ''}? Audio routing will briefly pause. The Windows process stays open.</p>
                        <button type="button" disabled={processBusy} onClick={async () => {
                            setProcessBusy(true); setProcessNotice('');
                            try { await onRemoveProcessConfiguration({ processName: selectedProcessName,
                                runtimePrefix, includeProfile: includesProfile });
                                setConfirmProcessRemoval(false); setSelectedNodeKey(null); }
                            catch (error) { setProcessNotice(error.message); }
                            finally { setProcessBusy(false); }
                        }}>{includesProfile && assignedProfile ? 'Remove connections and profile' : 'Remove process connections'}</button>
                        <button type="button" onClick={() => setConfirmProcessRemoval(false)}>Cancel</button>
                    </div>}
                </>}
                {processNotice && <p className="patchbay-status" role="status">{processNotice}</p>}
            </div> : <>
            {bulkOpen ? <div className="bulk-route-editor">
                <label className="bulk-route-search">Find connections
                    <input value={bulkSearch} onChange={event => setBulkSearch(event.target.value)} placeholder="Label or channel" />
                </label>
                <div className="bulk-route-list" role="group" aria-label="Connections to customize">
                    {bulkVisibleRoutes.length ? bulkVisibleRoutes.map(route => <label key={routeVisualKey(route)}>
                        <input className="bulk-route-checkbox" type="checkbox"
                            checked={bulkSelectedKeys.includes(routeVisualKey(route))}
                            onChange={event => { toggleBulkRoute(routeVisualKey(route), event.target.checked); setBulkNotice(''); }} />
                        <span>{describeRoute(route)}</span>
                    </label>) : <p>No matching connections.</p>}
                </div>
                <div className="bulk-route-actions">
                    <button type="button" onClick={() => setBulkSelectedKeys(current =>
                        [...new Set([...current, ...bulkVisibleRoutes.map(routeVisualKey)])])} disabled={!bulkVisibleRoutes.length}>Select shown</button>
                    <button type="button" onClick={() => setBulkSelectedKeys([])} disabled={!bulkSelectedRoutes.length}>Clear selection</button>
                </div>
                <p className="bulk-route-count">{bulkSelectedRoutes.length} {bulkSelectedRoutes.length === 1 ? 'connection' : 'connections'} selected</p>
                <label className="bulk-property-toggle"><input type="checkbox" checked={bulkUseLabel}
                    onChange={event => setBulkUseLabel(event.target.checked)} /> Apply label</label>
                <input className="bulk-label-input" maxLength="60" value={bulkLabel} placeholder="e.g. Headphone"
                    onChange={event => { setBulkLabel(event.target.value); setBulkUseLabel(true); setBulkNotice(''); }} />
                <label className="bulk-property-toggle"><input type="checkbox" checked={bulkUseColor}
                    onChange={event => setBulkUseColor(event.target.checked)} /> Apply color</label>
                <input className="bulk-color-input" type="color" value={bulkColor}
                    onChange={event => { setBulkColor(event.target.value); setBulkUseColor(true); setBulkNotice(''); }} />
                <button type="button" className="bulk-apply"
                    disabled={!bulkSelectedRoutes.length || (!bulkUseLabel && !bulkUseColor)}
                    onClick={() => {
                        onBulkChangeRouteVisuals(bulkSelectedRoutes, {
                            ...(bulkUseLabel ? { label: bulkLabel.slice(0, 60) } : {}),
                            ...(bulkUseColor ? { color: bulkColor } : {})
                        });
                        setBulkNotice(`Appearance updated for ${bulkSelectedRoutes.length} ${bulkSelectedRoutes.length === 1 ? 'connection' : 'connections'}.`);
                    }}>Apply appearance to selected</button>
                <p className="route-visual-note">Appearance only · audio routes are unchanged.</p>
                {bulkNotice && <p className="bulk-notice" role="status">{bulkNotice}</p>}
            </div> : <>
            <p className="patchbay-status" aria-live="polite">{selectedSourceId
                ? 'Source selected. Choose a compatible destination; press Escape to cancel.'
                : selectedRoute
                    ? `Selected connection: ${selectedVisual?.label ? `${selectedVisual.label} · ` : ''}${endpointNames.get(selectedRoute.sourceEndpointId) || selectedRoute.sourceEndpointId} → ${endpointNames.get(selectedRoute.destinationEndpointId) || selectedRoute.destinationEndpointId}${selectedRouteVisible ? '.' : ' (hidden by collapsed groups or filters).'}`
                    : `${routes.length} configured ${routes.length === 1 ? 'connection' : 'connections'}${hiddenRouteCount ? `; ${hiddenRouteCount} hidden by collapsed groups or filters` : ''}.`}</p>
            {selectedRoute ? <div className="patchbay-selection-actions">
            <div className="connection-state" aria-label="Selected connection state">
                <strong className={`connection-status ${routeState(selectedRoute)}`}>{routeState(selectedRoute) === 'active' ? '● Active' :
                    routeState(selectedRoute) === 'saved' ? '◉ Saved · waiting for channels' : '◌ Pending application'}</strong>
                <span>{selectedRoute.mute ? 'Muted' : 'Sound enabled'}</span>
                <span>{selectedRoute.gainDb > 0 ? `▲ +${selectedRoute.gainDb} dB gain` : `${selectedRoute.gainDb ?? 0} dB gain`}</span>
            </div>
            <div className="route-visual-editor">
                <label>Connection label
                    <input className="route-label-input" maxLength="60" value={selectedVisual?.label || ''}
                        placeholder="e.g. Headphone" onChange={event => onChangeRouteVisual(selectedRoute, 'label', event.target.value)} />
                </label>
                <label>Connection color
                    <input className="route-color-input" type="color" value={selectedVisual?.color || DEFAULT_ROUTE_COLOR}
                        onChange={event => onChangeRouteVisual(selectedRoute, 'color', event.target.value)} />
                </label>
                <span className="route-visual-note">Appearance only · saved locally for this source and destination.</span>
            </div>
            <div className="route-audio-editor">
                <label>Gain (dB)
                    <input className="route-gain-input" type="number" min="-120" max="24" step="0.1"
                        disabled={disabled} value={selectedRoute.gainDb ?? 0}
                        onChange={event => onEditRoute(selectedRoute.id, 'gainDb', Number(event.target.value))} />
                </label>
                <label className="route-mute-label"><input className="route-mute-input" type="checkbox"
                    disabled={disabled} checked={!!selectedRoute.mute}
                    onChange={event => onEditRoute(selectedRoute.id, 'mute', event.target.checked)} /> Mute</label>
            </div>
            {!selectedRouteVisible && <button type="button" className="reveal-selected-connection" onClick={revealSelectedRoute}>
                Show selected connection
            </button>}
            <button type="button" className="inspect-connection-matrix" ref={matrixTriggerRef} aria-haspopup="dialog"
                onClick={() => setMatrixOpen(true)}>Inspect channel matrix</button>
            <button type="button" className="remove-selected-connection" disabled={disabled}
                onClick={() => {
                    onToggleRoute(selectedRoute.sourceEndpointId, selectedRoute.destinationEndpointId);
                    setConnectionDialogOpen(false);
                    onSelectRoute(null);
                }}>Remove selected connection</button>
            </div> : <p className="patchbay-inspector-empty">Select a cable or choose a connection above to edit it.</p>}
            </>}
            </>}
        </aside></div>, document.body)}
        </div>
        {matrixOpen && matrixSource && matrixDestination && createPortal(<div className="connection-matrix-backdrop" onMouseDown={event => {
            if (event.target === event.currentTarget) setMatrixOpen(false);
        }}>
            <div className="connection-matrix" role="dialog" aria-modal="true" aria-labelledby="connection-matrix-title">
                <div className="connection-matrix-header"><div>
                    <h2 id="connection-matrix-title">Channel matrix</h2>
                    <p>{matrixSource.label} → {matrixDestination.label}</p>
                </div><button type="button" ref={matrixCloseRef} onClick={() => setMatrixOpen(false)} aria-label="Close channel matrix">Close</button></div>
                <div className="connection-matrix-scroll"><table><thead><tr><th scope="col">Output / input</th>
                    {matrixDestination.endpoints.filter(endpoint => matrixRoutes.some(route => route.destinationEndpointId === endpoint.id)).map(endpoint =>
                        <th key={endpoint.id} scope="col" title={endpoint.name}>{endpoint.name}</th>)}</tr></thead><tbody>
                    {matrixSource.endpoints.filter(endpoint => matrixRoutes.some(route => route.sourceEndpointId === endpoint.id)).map(source =>
                        <tr key={source.id}><th scope="row" title={source.name}>{source.name}</th>
                            {matrixDestination.endpoints.filter(endpoint => matrixRoutes.some(route => route.destinationEndpointId === endpoint.id)).map(destination => {
                                const linked = matrixRoutes.some(route => route.sourceEndpointId === source.id && route.destinationEndpointId === destination.id);
                                return <td key={destination.id}>{linked ? '●' : '·'}</td>;
                            })}</tr>)}</tbody></table></div>
            </div></div>, document.body)}
        {processDialogNode && createPortal(<div className="routing-dialog-backdrop" onMouseDown={event => {
            if (event.target === event.currentTarget) setProcessDialogKey(null);
        }}><div className="routing-dialog" role="dialog" aria-modal="true" aria-label="Process connections">
            <div className="routing-dialog-heading"><h2>{processDialogNode.label} · Connections</h2>
                <button type="button" onClick={() => setProcessDialogKey(null)}>Close</button></div>
            <p className="hint">Choose one connection to edit or remove. Removing changes the draft until you apply the configuration.</p>
            <div className="process-dialog-routes">{processDialogRoutes.map(route => <div key={route.id}>
                <span>{describeRoute(route)}</span>
                <button type="button" onClick={() => { setSelectedNodeKey(null); setBulkOpen(false);
                    onSelectRoute(route.id); setConnectionDialogOpen(true); setProcessDialogKey(null); }}>Edit…</button>
                <button type="button" disabled={disabled} onClick={() =>
                    onToggleRoute(route.sourceEndpointId, route.destinationEndpointId)}>Remove</button>
            </div>)}</div>
        </div></div>, document.body)}
        {contextMenu && createPortal(<div className="patchbay-context-menu" role="menu"
            aria-label={contextMenu.kind === 'pane' ? 'Graph actions' : contextMenu.kind === 'node' ? 'Process actions' : 'Connection actions'}
            style={{ left: Math.max(8, contextMenu.x), top: Math.max(8, contextMenu.y) }}
            onPointerDown={event => event.stopPropagation()}>
            {contextMenu.kind === 'pane' && <button type="button" role="menuitem"
                onClick={() => menuAction(onCreateConnection)}>Create a new connection</button>}
            {contextMenu.kind === 'pane' && onCreateBridge && <button type="button" role="menuitem" disabled={disabled}
                onClick={() => menuAction(onCreateBridge)}>Add Windows audio bridge…</button>}
            {contextMenu.kind === 'pane' && <button type="button" role="menuitem"
                onClick={() => menuAction(() => onShowProcessDetail(null))}>Process and channel detail</button>}
            {contextMenu.kind === 'pane' && routes.length > 0 && <button type="button" role="menuitem"
                onClick={() => menuAction(() => { setBulkSelectedKeys([]); setSelectedNodeKey(null);
                    setConnectionDialogOpen(false); setBulkOpen(true); })}>Edit appearance of several connections…</button>}
            {contextNode && <>
                <strong>{contextNode.label}</strong>
                <button type="button" role="menuitem" onClick={() => menuAction(() => {
                    const layout = nodeLayout[contextNode.key] || {};
                    setEditingNodeKey(contextNode.key);
                    setRenameDraft(layout.label || contextNode.label);
                })}>Rename block…</button>
                {!['physical', 'wasapi'].includes(contextNode.type) && <>
                <button type="button" role="menuitem" onClick={() => menuAction(() => {
                    setSelectedNodeKey(contextNode.key); setConnectionDialogOpen(false);
                    setBulkOpen(false); setConfirmProcessRemoval(false); setProcessNotice('');
                    onSelectRoute(null);
                })}>Configure process…</button>
                <button type="button" role="menuitem" onClick={() => menuAction(() =>
                    onCreateConnection(contextNode.label.split(' · ')[0].trim()))}>Add a connection…</button>
                <button type="button" role="menuitem" disabled={!contextNodeRoutes.length}
                    onClick={() => menuAction(() => setProcessDialogKey(contextNode.key))}>Remove a connection…</button>
                <button type="button" role="menuitem" disabled={!contextNodeRoutes.length}
                    onClick={() => menuAction(() => setProcessDialogKey(contextNode.key))}>Modify a connection…</button>
                <button type="button" role="menuitem" disabled={!contextNodeRoutes.length}
                    onClick={() => menuAction(() => { setBulkSelectedKeys(contextNodeRoutes.map(routeVisualKey));
                        setBulkOpen(true); setSelectedNodeKey(null); })}>Edit appearance of process connections…</button>
                <button type="button" role="menuitem" onClick={() => menuAction(() =>
                    onShowProcessDetail(contextNode.label.split(' · ')[0].trim()))}>Process and channel detail</button>
                </>}
                {contextNode.type === 'physical' && <button type="button" role="menuitem"
                    onClick={() => menuAction(onCreateConnection)}>Add a connection…</button>}
                {contextNode.type === 'wasapi' && <>
                    <button type="button" role="menuitem" disabled={disabled} onClick={() => menuAction(() => onEditBridge(contextNode.bridgeId))}>Configure Windows audio bridge…</button>
                    <button type="button" role="menuitem" disabled={disabled} onClick={() => menuAction(() => onRemoveBridge(contextNode.bridgeId))}>Remove Windows audio bridge…</button>
                    <button type="button" role="menuitem" disabled={disabled} onClick={() => menuAction(onCreateConnection)}>Add a connection…</button>
                </>}
            </>}
            {contextRoute && <>
                <strong>{describeRoute(contextRoute)}</strong>
                <button type="button" role="menuitem" onClick={() => menuAction(() => {
                    setSelectedNodeKey(null); setBulkOpen(false); onSelectRoute(contextRoute.id);
                    setConnectionDialogOpen(true);
                })}>Edit this connection…</button>
                <button type="button" role="menuitem" onClick={() => menuAction(() => {
                    setSelectedNodeKey(null); setBulkOpen(false); setConnectionDialogOpen(false);
                    onSelectRoute(contextRoute.id); setMatrixOpen(true);
                })}>Inspect channel matrix</button>
                <button type="button" role="menuitem" disabled={disabled}
                    onClick={() => menuAction(() => onEditRoute(contextRoute.id, 'mute', !contextRoute.mute))}>
                    {contextRoute.mute ? 'Unmute this connection' : 'Mute this connection'}</button>
                <button type="button" role="menuitem" disabled={disabled}
                    onClick={() => menuAction(() => {
                        onToggleRoute(contextRoute.sourceEndpointId, contextRoute.destinationEndpointId);
                        setConnectionDialogOpen(false);
                        if (selectedRouteId === contextRoute.id) onSelectRoute(null);
                    })}>Remove this connection</button>
            </>}
        </div>, document.body)}
    </div>;
}
