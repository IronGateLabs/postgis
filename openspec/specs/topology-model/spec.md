## Purpose

Defines the PostGIS Topology data model and SQL API as implemented in the `postgis_topology` extension. This covers the topology schema structure (nodes, edges, faces, relation tables), topology management functions (CreateTopology, DropTopology), ISO SQL/MM topology primitives for mutation and query, the TopoGeometry composite type, high-level population functions, and topology validation. The topology model implements a planar graph where every edge bounds exactly two faces (including the universal face 0), enabling topologically-consistent spatial data storage.

## ADDED Requirements

### Requirement: Topology schema and metadata tables
The system SHALL maintain a `topology.topology` metadata table with columns: `id` (serial primary key), `name` (unique varchar), `SRID` (integer), `precision` (float8), `hasz` (boolean, default false), `useslargeids` (boolean, default false). A `topology.layer` table SHALL track registered TopoGeometry columns with columns: `topology_id`, `layer_id`, `schema_name`, `table_name`, `feature_column`, `feature_type`, `level`, `child_id`.

When CreateTopology is called, a new PostgreSQL schema SHALL be created containing four tables: `node` (node_id, containing_face, geom), `edge_data` (edge_id, start_node, end_node, next_left_edge, next_right_edge, left_face, right_face, geom), `face` (face_id, mbr), and `relation` (topogeo_id, layer_id, element_id, element_type).

#### Scenario: CreateTopology creates schema with required tables
- **GIVEN** no topology named 'test_topo' exists
- **WHEN** `SELECT topology.CreateTopology('test_topo', 4326, 0.0001)` is executed
- **THEN** a schema 'test_topo' SHALL exist with tables node, edge_data, face, and relation
- **AND** a row SHALL be inserted into topology.topology with name='test_topo', SRID=4326, precision=0.0001
- Validated by: topology/test/regress/createtopology.sql

#### Scenario: CreateTopology with hasZ flag
- **GIVEN** no topology named 'topo3d' exists
- **WHEN** `SELECT topology.CreateTopology('topo3d', 4326, 0, true)` is executed
- **THEN** the topology.topology row SHALL have hasz=true
- **AND** the node.geom and edge_data.geom columns SHALL accept 3D geometries
- Validated by: topology/test/regress/topo2.5d.sql

#### Scenario: CreateTopology initializes universal face
- **GIVEN** a new topology is created
- **WHEN** the face table is queried
- **THEN** face_id=0 (the universal/unbounded face) SHALL exist
- **AND** its mbr SHALL be NULL
- Validated by: topology/test/regress/createtopology.sql

### Requirement: DropTopology removes schema and metadata
`topology.DropTopology(name)` SHALL drop the named schema (CASCADE) and remove the corresponding row from `topology.topology` and all associated rows from `topology.layer`.

#### Scenario: DropTopology removes schema and metadata row
- **GIVEN** a topology 'drop_me' exists
- **WHEN** `SELECT topology.DropTopology('drop_me')` is executed
- **THEN** the schema 'drop_me' SHALL no longer exist
- **AND** no row with name='drop_me' SHALL remain in topology.topology
- Validated by: topology/test/regress/droptopology.sql

#### Scenario: DropTopology on non-existent topology raises error
- **WHEN** `SELECT topology.DropTopology('no_such_topo')` is executed
- **THEN** an error SHALL be raised indicating the topology does not exist
- Validated by: topology/test/regress/droptopology.sql

#### Scenario: DropTopology with registered layers
- **GIVEN** a topology with a registered TopoGeometry column
- **WHEN** DropTopology is called
- **THEN** the layer registration SHALL be removed and the schema dropped
- Validated by: topology/test/regress/droptopology.sql

### Requirement: ST_AddIsoNode adds isolated node to a face
`topology.ST_AddIsoNode(toponame, face_id, point)` SHALL add a node not connected to any edge within the specified face. The point must lie within the specified face (or face 0 for the universal face). The function returns the new node_id. An error SHALL be raised if the point geometry is not a POINT, if it falls outside the specified face, or if it coincides with an existing node.

#### Scenario: Add isolated node to universal face
- **GIVEN** an empty topology 'city_topo'
- **WHEN** `SELECT topology.ST_AddIsoNode('city_topo', 0, 'POINT(1 2)')` is executed
- **THEN** a new node SHALL be created with containing_face=0
- **AND** the returned node_id SHALL be a positive integer
- Validated by: topology/test/regress/st_addisonode.sql

#### Scenario: Add isolated node with wrong face raises error
- **GIVEN** a topology with face_id=1 bounded by edges
- **WHEN** ST_AddIsoNode is called with a point outside face 1 but referencing face_id=1
- **THEN** an error SHALL be raised indicating the point is not within the face
- Validated by: topology/test/regress/st_addisonode.sql

#### Scenario: Add node at existing node location raises error
- **GIVEN** a topology with an existing node at POINT(1 2)
- **WHEN** `ST_AddIsoNode('topo', 0, 'POINT(1 2)')` is called
- **THEN** an error SHALL be raised about a coincident node
- Validated by: topology/test/regress/st_addisonode.sql

### Requirement: ST_AddIsoEdge connects two isolated nodes
`topology.ST_AddIsoEdge(toponame, node1, node2, linestring)` SHALL create an edge between two existing isolated nodes. Both nodes must be isolated (not connected to any other edge), both must be in the same face, and the linestring must not cross any existing edge. The function returns the new edge_id. After insertion the nodes stop being isolated (containing_face is reset).

#### Scenario: Connect two isolated nodes with an edge
- **GIVEN** a topology with two isolated nodes n1 and n2 in the same face
- **WHEN** `ST_AddIsoEdge('topo', n1, n2, linestring)` is called with a linestring from n1's point to n2's point
- **THEN** a new edge SHALL be created connecting n1 and n2
- **AND** the nodes' containing_face SHALL be set to NULL
- Validated by: topology/test/regress/st_addisoedge.sql

#### Scenario: AddIsoEdge with crossing edge raises error
- **GIVEN** an existing edge in the topology
- **WHEN** ST_AddIsoEdge is called with a linestring that crosses the existing edge
- **THEN** an error SHALL be raised about edge crossing
- Validated by: topology/test/regress/st_addisoedge.sql

#### Scenario: AddIsoEdge refuses closed edge
- **WHEN** ST_AddIsoEdge is called with the same node for both endpoints
- **THEN** an error SHALL be raised because a closed isolated edge would create a ring
- Validated by: topology/test/regress/st_addisoedge.sql

### Requirement: ST_AddEdgeModFace adds edge splitting an existing face
`topology.ST_AddEdgeModFace(toponame, node1, node2, linestring)` SHALL add a new edge and, if the edge splits a face, modify the existing face (keeping its face_id) and create a new face for the other portion. The Relation table SHALL be updated to reflect any face splits. Returns the new edge_id.

#### Scenario: Add edge that splits a face
- **GIVEN** a topology with a bounded face and two nodes on its boundary
- **WHEN** `ST_AddEdgeModFace('topo', n1, n2, line)` is called
- **THEN** a new edge SHALL be created
- **AND** the original face SHALL be modified and a new face created
- **AND** edge left_face and right_face fields SHALL reference the correct faces
- Validated by: topology/test/regress/st_addedgemodface.sql

#### Scenario: AddEdgeModFace updates Relation table
- **GIVEN** a TopoGeometry defined by a face that gets split
- **WHEN** ST_AddEdgeModFace splits that face
- **THEN** the Relation table SHALL be updated so the TopoGeometry references both resulting faces
- Validated by: topology/test/regress/st_addedgemodface.sql

#### Scenario: AddEdgeModFace within universal face
- **GIVEN** a topology with only isolated nodes in the universal face
- **WHEN** ST_AddEdgeModFace connects two nodes
- **THEN** the universal face (0) SHALL remain and a new bounded face SHALL be created
- Validated by: topology/test/regress/st_addedgemodface.sql

### Requirement: ST_AddEdgeNewFaces adds edge creating new faces for both sides
`topology.ST_AddEdgeNewFaces(toponame, node1, node2, linestring)` SHALL add a new edge and, if the edge splits a face, remove the old face and create two new faces. This differs from ST_AddEdgeModFace in that neither new face reuses the old face_id. Returns the new edge_id.

#### Scenario: AddEdgeNewFaces creates two new faces
- **GIVEN** a topology with a bounded face
- **WHEN** ST_AddEdgeNewFaces splits it
- **THEN** the old face SHALL be removed and two new faces created with new face_ids
- Validated by: topology/test/regress/st_addedgenewfaces.sql

#### Scenario: AddEdgeNewFaces updates Relation table
- **GIVEN** a TopoGeometry referencing a face that gets split
- **WHEN** ST_AddEdgeNewFaces is called
- **THEN** the Relation table SHALL be updated to reference the two new faces
- Validated by: topology/test/regress/st_addedgenewfaces.sql

#### Scenario: AddEdgeNewFaces with no face split
- **GIVEN** two nodes connected that do not enclose a new area
- **WHEN** ST_AddEdgeNewFaces is called
- **THEN** no new face SHALL be created beyond updating edge references
- Validated by: topology/test/regress/st_addedgenewfaces.sql

### Requirement: Edge splitting and healing operations
`ST_ModEdgeSplit(toponame, edge_id, point)` SHALL split an existing edge at the given point, creating a new node and a new edge. The original edge is modified to end at the new node. `ST_NewEdgesSplit` is similar but replaces the original edge with two new edges. `ST_ModEdgeHeal` and `ST_NewEdgeHeal` merge two adjacent edges sharing a node, removing the shared node.

#### Scenario: ST_ModEdgeSplit splits edge preserving original edge_id
- **GIVEN** a topology with edge e1
- **WHEN** `ST_ModEdgeSplit('topo', e1, split_point)` is called
- **THEN** edge e1 SHALL be modified to end at the new node
- **AND** a new edge SHALL be created from the new node to the original end node
- **AND** a new node SHALL be created at split_point
- Validated by: topology/test/regress/st_modedgesplit.sql

#### Scenario: ST_NewEdgesSplit replaces original edge with two new edges
- **GIVEN** a topology with edge e1
- **WHEN** `ST_NewEdgesSplit('topo', e1, split_point)` is called
- **THEN** edge e1 SHALL be removed
- **AND** two new edges SHALL be created meeting at the new split node
- Validated by: topology/test/regress/st_newedgessplit.sql

#### Scenario: ST_ModEdgeHeal merges two edges
- **GIVEN** two edges e1 and e2 sharing a node that has degree 2
- **WHEN** `ST_ModEdgeHeal('topo', e1, e2)` is called
- **THEN** one edge SHALL be removed, the other extended to cover both
- **AND** the shared node SHALL be removed
- Validated by: topology/test/regress/st_modedgeheal.sql

### Requirement: Edge and node removal operations
`ST_RemEdgeModFace(toponame, edge_id)` SHALL remove an edge, merging the two faces on either side by keeping one face_id. `ST_RemEdgeNewFace` removes the edge and creates a new face replacing both. `ST_RemoveIsoNode` removes an isolated node. `ST_RemoveIsoEdge` removes an isolated edge (one whose removal does not affect face definitions).

#### Scenario: ST_RemEdgeModFace merges faces keeping one face_id
- **GIVEN** an edge separating face A and face B
- **WHEN** `ST_RemEdgeModFace('topo', edge_id)` is called
- **THEN** the edge SHALL be removed
- **AND** one face SHALL remain (the other removed), absorbing the area of both
- Validated by: topology/test/regress/st_remedgemodface.sql

#### Scenario: ST_RemoveIsoNode removes isolated node
- **GIVEN** an isolated node (not connected to any edge)
- **WHEN** `ST_RemoveIsoNode('topo', node_id)` is called
- **THEN** the node SHALL be removed from the node table
- Validated by: topology/test/regress/st_removeisonode.sql

#### Scenario: ST_RemoveIsoNode on connected node raises error
- **GIVEN** a node that is an endpoint of at least one edge
- **WHEN** ST_RemoveIsoNode is called on it
- **THEN** an error SHALL be raised indicating the node is not isolated
- Validated by: topology/test/regress/st_removeisonode.sql

### Requirement: Topology query functions
`ST_GetFaceEdges(toponame, face_id)` SHALL return the ordered set of signed edge identifiers bounding a face. `ST_GetFaceGeometry(toponame, face_id)` SHALL return the polygon geometry for a face computed from its bounding edges. `GetNodeByPoint(toponame, point, tolerance)` SHALL return the node_id of a node within the given tolerance of the point, or raise an error if multiple nodes match. `GetFaceByPoint` and `GetEdgeByPoint` work similarly for faces and edges.

#### Scenario: ST_GetFaceEdges returns ordered signed edges
- **GIVEN** a topology with a triangular face bounded by edges e1, e2, e3
- **WHEN** `ST_GetFaceEdges('topo', face_id)` is called
- **THEN** it SHALL return 3 rows with signed edge ids indicating traversal direction
- **AND** the sequence numbers SHALL start at 1
- Validated by: topology/test/regress/st_getfaceedges.sql

#### Scenario: ST_GetFaceGeometry returns polygon for face
- **GIVEN** a face defined by bounding edges
- **WHEN** `ST_GetFaceGeometry('topo', face_id)` is called
- **THEN** the result SHALL be a POLYGON geometry representing the face area
- Validated by: topology/test/regress/st_getfacegeometry.sql

#### Scenario: GetNodeByPoint finds node within tolerance
- **GIVEN** a node at POINT(1 2) in topology 'topo'
- **WHEN** `GetNodeByPoint('topo', 'POINT(1.0001 2)', 0.001)` is called
- **THEN** the node_id of the node at POINT(1 2) SHALL be returned
- Validated by: topology/test/regress/getnodebypoint.sql

### Requirement: TopoGeometry type and creation
The `topology.TopoGeometry` composite type SHALL consist of (topology_id integer, layer_id integer, id bigint, type integer) where type is 1=point, 2=line, 3=polygon, 4=collection. TopoGeometry objects are created via `CreateTopoGeom(toponame, geomtype, layer_id, topo_elements)` where topo_elements is a TopoElementArray. The `TopoElement` domain is a bigint[2] array of (element_id, element_type) where element_type is 1=node, 2=edge, 3=face for simple TopoGeometries.

#### Scenario: CreateTopoGeom creates a polygon TopoGeometry from face elements
- **GIVEN** a topology with face_id=1
- **WHEN** `CreateTopoGeom('topo', 3, layer_id, '{{1,3}}')` is called
- **THEN** a TopoGeometry SHALL be returned with type=3
- **AND** the Relation table SHALL contain a row linking the topogeom to face 1
- Validated by: topology/test/regress/createtopogeom.sql

#### Scenario: TopoGeometry cast to geometry
- **GIVEN** a TopoGeometry of type polygon referencing faces
- **WHEN** the TopoGeometry is cast to geometry
- **THEN** the result SHALL be the geometry formed by merging the referenced face geometries
- Validated by: topology/test/regress/geometry_cast.sql

#### Scenario: TopoElement domain enforces array constraints
- **WHEN** a TopoElement value with more than 2 elements is inserted
- **THEN** a constraint violation error SHALL be raised
- **AND** a TopoElement with element_type <= 0 SHALL also be rejected
- Validated by: topology/test/regress/topoelement.sql

### Requirement: TopoGeo_AddPoint, TopoGeo_AddLinestring, TopoGeo_AddPolygon
High-level population functions SHALL snap input geometry to existing topology features within the topology's tolerance, split edges as needed, and add new nodes/edges/faces. `TopoGeo_AddPoint` returns the node_id of the added (or snapped-to) node. `TopoGeo_AddLinestring` returns a set of edge_ids. `TopoGeo_AddPolygon` returns a set of face_ids.

#### Scenario: TopoGeo_AddPoint snaps to existing node
- **GIVEN** a topology with tolerance 1.0 and a node at POINT(0 0)
- **WHEN** `TopoGeo_AddPoint('topo', 'POINT(0.5 0)')` is called
- **THEN** the existing node_id SHALL be returned (no new node created)
- Validated by: topology/test/regress/topogeo_addpoint.sql

#### Scenario: TopoGeo_AddLinestring splits existing edges
- **GIVEN** a topology with an existing edge
- **WHEN** TopoGeo_AddLinestring adds a line that crosses the existing edge
- **THEN** the existing edge SHALL be split at the intersection
- **AND** the new line SHALL be added as one or more edges
- Validated by: topology/test/regress/topogeo_addlinestring.sql

#### Scenario: TopoGeo_AddPolygon registers face
- **GIVEN** an empty topology
- **WHEN** `TopoGeo_AddPolygon('topo', polygon_geom)` is called
- **THEN** the polygon boundary SHALL be added as edges
- **AND** the resulting face_id(s) SHALL be returned
- Validated by: topology/test/regress/topogeo_addpolygon.sql

### Requirement: toTopoGeom converts geometry to TopoGeometry
`topology.toTopoGeom(geometry, toponame, layer_id, tolerance)` SHALL convert a geometry into a TopoGeometry by adding the geometry's primitives to the topology (splitting existing edges as needed) and registering the resulting topology elements in the Relation table for the specified layer.

#### Scenario: toTopoGeom converts polygon to TopoGeometry
- **GIVEN** a topology with a registered polygon layer
- **WHEN** `toTopoGeom(polygon_geom, 'topo', layer_id)` is called
- **THEN** a TopoGeometry SHALL be returned
- **AND** the topology SHALL contain edges forming the polygon boundary and a face for the polygon interior
- Validated by: topology/test/regress/totopogeom.sql

#### Scenario: toTopoGeom with existing TopoGeometry replaces elements
- **GIVEN** an existing TopoGeometry tg
- **WHEN** `toTopoGeom(new_geom, tg)` is called (two-argument form)
- **THEN** the existing TopoGeometry's elements SHALL be replaced by the new geometry's primitives
- Validated by: topology/test/regress/totopogeom.sql

#### Scenario: clearTopoGeom removes all elements
- **GIVEN** a TopoGeometry with registered elements
- **WHEN** `clearTopoGeom(tg)` is called
- **THEN** all rows for that TopoGeometry SHALL be removed from the Relation table
- **AND** the TopoGeometry id SHALL remain valid
- Validated by: topology/test/regress/cleartopogeom.sql

### Requirement: ValidateTopology checks integrity
`topology.ValidateTopology(toponame, bbox)` SHALL check the topology for internal consistency and return a set of (error text, id1 bigint, id2 bigint) rows describing each detected problem. Checks include: edges crossing without a node, edge endpoints not matching their start/end nodes, face geometry mismatches, and dangling edge/node references.

#### Scenario: Valid topology returns no errors
- **GIVEN** a correctly constructed topology
- **WHEN** `ValidateTopology('topo')` is called
- **THEN** the result set SHALL be empty
- Validated by: topology/test/regress/validatetopology.sql

#### Scenario: Crossing edges detected
- **GIVEN** a topology where two edges cross without a node at the intersection (corrupt data)
- **WHEN** ValidateTopology is called
- **THEN** at least one error row SHALL be returned describing the crossing edges
- Validated by: topology/test/regress/validatetopology.sql

#### Scenario: ValidateTopology with bbox limits check area
- **GIVEN** a large topology
- **WHEN** `ValidateTopology('topo', 'POLYGON((0 0,10 0,10 10,0 10,0 0))')` is called with a bounding box
- **THEN** only topology primitives intersecting the bbox SHALL be checked
- Validated by: topology/test/regress/validatetopology_large.sql

### Requirement: ST_ChangeEdgeGeom modifies edge geometry
`topology.ST_ChangeEdgeGeom(toponame, edge_id, linestring)` SHALL update the geometry of an existing edge. The new geometry must start and end at the same nodes as the original. The operation must be topologically safe: the new geometry must not cross other edges and must not change the topological relationships (the edge must still separate the same faces).

#### Scenario: Change edge geometry preserving topology
- **GIVEN** an edge connecting nodes n1 and n2
- **WHEN** ST_ChangeEdgeGeom is called with a new linestring from n1 to n2 that does not cross other edges
- **THEN** the edge geometry SHALL be updated
- **AND** left_face and right_face SHALL remain unchanged
- Validated by: topology/test/regress/st_changeedgegeom.sql

#### Scenario: ChangeEdgeGeom with wrong endpoints raises error
- **WHEN** ST_ChangeEdgeGeom is called with a linestring that does not start at the edge's start_node
- **THEN** an error SHALL be raised
- Validated by: topology/test/regress/st_changeedgegeom.sql

#### Scenario: ChangeEdgeGeom crossing another edge raises error
- **WHEN** the new geometry crosses an existing edge
- **THEN** an error SHALL be raised indicating the move is not topologically safe
- Validated by: topology/test/regress/st_changeedgegeom.sql

### Requirement: Topology snap tolerance and precision model
When a topology is created with a non-zero precision value, all operations (AddPoint, AddLinestring, etc.) SHALL use that precision as the snap tolerance for point matching and edge snapping. When precision is 0, a minimum tolerance is computed based on the coordinate magnitude using `_st_mintolerance()`. This ensures that coordinates within the tolerance are considered coincident.

#### Scenario: Non-zero precision snaps coordinates
- **GIVEN** a topology with precision=1.0
- **WHEN** TopoGeo_AddPoint is called with a point within 1.0 of an existing node
- **THEN** the existing node SHALL be returned instead of creating a new node
- Validated by: topology/test/regress/topogeo_addpoint.sql

#### Scenario: Zero precision uses computed minimum tolerance
- **GIVEN** a topology with precision=0
- **WHEN** the internal tolerance is computed for coordinate magnitude ~100
- **THEN** the tolerance SHALL be approximately 3.6e-13 (based on _st_mintolerance formula)
- Status: untested -- tolerance computation is internal; no direct regression test

#### Scenario: ValidateTopologyPrecision checks coordinate alignment
- **GIVEN** a topology with precision=1.0 but node coordinates not aligned to grid
- **WHEN** `ValidateTopologyPrecision('topo')` is called
- **THEN** errors SHALL be returned for coordinates not aligned to the precision grid
- Validated by: topology/test/regress/validatetopologyprecision.sql

### Requirement: ST_MoveIsoNode relocates an isolated node
`topology.ST_MoveIsoNode(toponame, node_id, point)` SHALL move an isolated node to a new location. The node must be isolated (not connected to any edge). The new location must not coincide with another existing node and must be within the same face.

#### Scenario: Move isolated node to new position
- **GIVEN** an isolated node in a topology
- **WHEN** ST_MoveIsoNode is called with a valid new position
- **THEN** the node's geometry SHALL be updated to the new position
- Validated by: topology/test/regress/st_moveisonode.sql

#### Scenario: MoveIsoNode on non-isolated node raises error
- **GIVEN** a node connected to edges
- **WHEN** ST_MoveIsoNode is called
- **THEN** an error SHALL be raised indicating the node is not isolated
- Validated by: topology/test/regress/st_moveisonode.sql

#### Scenario: MoveIsoNode to location of existing node raises error
- **GIVEN** two isolated nodes
- **WHEN** ST_MoveIsoNode tries to move one to the other's location
- **THEN** an error SHALL be raised about a coincident node
- Validated by: topology/test/regress/st_moveisonode.sql

### Requirement: AddTopoGeometryColumn and layer registration
`topology.AddTopoGeometryColumn(toponame, schema, table, column, geomtype)` SHALL add a TopoGeometry column to the specified table and register it as a layer in the `topology.layer` table. The function returns the layer_id. `DropTopoGeometryColumn` SHALL remove the column and unregister the layer.

#### Scenario: AddTopoGeometryColumn registers layer
- **GIVEN** a table 'parcels' and a topology 'city'
- **WHEN** `AddTopoGeometryColumn('city', 'public', 'parcels', 'topo', 'POLYGON')` is called
- **THEN** a layer_id SHALL be returned
- **AND** a row SHALL exist in topology.layer linking the table column to the topology
- Validated by: topology/test/regress/addtopogeometrycolumn.sql

#### Scenario: DropTopoGeometryColumn removes layer
- **GIVEN** a registered TopoGeometry column
- **WHEN** DropTopoGeometryColumn is called
- **THEN** the column SHALL be dropped from the table
- **AND** the layer row SHALL be removed from topology.layer
- Validated by: topology/test/regress/droptopogeometrycolumn.sql

#### Scenario: Layer trigger prevents deletion with features
- **GIVEN** a layer with existing TopoGeometry features
- **WHEN** an attempt is made to delete the layer row directly from topology.layer
- **THEN** the trigger SHALL prevent deletion and raise a notice
- Validated by: topology/test/regress/layertrigger.sql
