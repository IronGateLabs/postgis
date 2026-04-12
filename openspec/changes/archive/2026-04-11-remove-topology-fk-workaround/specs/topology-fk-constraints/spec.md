## ADDED Requirements

### Requirement: Immediate foreign-key constraints on node and edge_data

The PostGIS topology `node` and `edge_data` tables SHALL declare the following foreign-key
constraints as immediate (not deferrable) when a topology is created via `CreateTopology`:

- `node.face_exists` → `face(face_id)`
- `edge_data.start_node_exists` → `node(node_id)`
- `edge_data.end_node_exists` → `node(node_id)`
- `edge_data.left_face_exists` → `face(face_id)`
- `edge_data.right_face_exists` → `face(face_id)`

Immediate semantics mean referential-integrity violations SHALL surface at the INSERT /
UPDATE statement that introduces them, not at transaction commit. This matches standard
PostgreSQL FK behavior and the historical (pre-workaround) PostGIS contract.

#### Scenario: Fresh topology has immediate FK constraints on node and edge_data

- **WHEN** a user creates a new topology via `SELECT topology.CreateTopology('my_topo', 4326)`
- **THEN** the generated `my_topo.node` and `my_topo.edge_data` tables SHALL contain five
  foreign-key constraints (`face_exists`, `start_node_exists`, `end_node_exists`,
  `left_face_exists`, `right_face_exists`) declared without a `DEFERRABLE` clause
- **AND** querying `pg_constraint.condeferrable` for those five constraints SHALL return
  `false`
- **AND** an INSERT into `my_topo.edge_data` referencing a non-existent `start_node` value
  SHALL raise a `foreign_key_violation` error at the INSERT statement, not at commit

### Requirement: Deferred foreign-key constraints on edge_data next-edge links

The PostGIS topology `edge_data` table SHALL declare `next_left_edge_exists` and
`next_right_edge_exists` as `DEFERRABLE INITIALLY DEFERRED`. These two constraints
reference other rows in the same `edge_data` table (via `abs_next_left_edge` /
`abs_next_right_edge`) and would otherwise prevent topology construction code from
inserting a set of edges whose forward links point at edges not yet inserted within the
same transaction.

This deferred behavior predates the PG 19devel workaround and is retained because it
reflects a real topology semantic need, not a bug workaround.

#### Scenario: next_left_edge and next_right_edge constraints are deferred

- **WHEN** a user creates a new topology via `SELECT topology.CreateTopology('my_topo', 4326)`
- **THEN** the generated `my_topo.edge_data` table SHALL contain
  `next_left_edge_exists` and `next_right_edge_exists` foreign-key constraints declared
  with `DEFERRABLE INITIALLY DEFERRED`
- **AND** querying `pg_constraint.condeferrable` for those two constraints SHALL return
  `true` and `pg_constraint.condeferred` SHALL return `true`

#### Scenario: Topology construction can insert edges with forward next-edge references

- **GIVEN** a transaction that inserts two edges A and B into `edge_data`
- **AND** edge A's `abs_next_left_edge` points at edge B
- **AND** edge A is inserted before edge B
- **WHEN** the transaction commits
- **THEN** the commit SHALL succeed
- **AND** no `foreign_key_violation` SHALL be raised, because the
  `next_left_edge_exists` FK is checked only at commit time by virtue of being deferred

### Requirement: Topology FK semantics match upstream PostGIS

The topology FK constraint declarations in this repository SHALL match the constraint
declarations shipped in upstream `postgis/postgis` for the same PostgreSQL target version.
The repository SHALL NOT carry FK-semantics divergences from upstream without an
accompanying tracking issue and a documented removal plan tied to an upstream fix.

#### Scenario: No divergence from upstream FK semantics in a released branch

- **WHEN** the `develop` branch of this repository is compared against
  `postgis/postgis#master` for the file `topology/sql/manage/CreateTopology.sql.in`
- **THEN** the FK constraint declarations (constraint name, referenced column, deferred
  vs immediate) in `node` and `edge_data` creation SHALL be identical
- **EXCEPT** where a divergence is documented in an active OpenSpec change with an
  explicit removal plan tied to an upstream fix that has not yet landed
