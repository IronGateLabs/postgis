## Purpose

Defines the requirements for the spec extraction process itself: what constitutes a complete spec, how to validate extraction quality, naming conventions for spec directories and requirements, and rules for cross-referencing between specs and between specs and regression tests.

This is a meta-spec -- it governs how the 12 capability specs listed in the design are written, not what they contain.

## Requirements

### Requirement: Spec directory structure
Each extracted capability SHALL be placed in its own directory under `openspec/specs/` containing a single `spec.md` file. The directory name SHALL use lowercase kebab-case matching the capability name from the design (e.g., `geometry-types`, `spatial-predicates`, `gserialized-format`).

#### Scenario: New spec directory created for geometry types
- **WHEN** the geometry-types spec is extracted
- **THEN** the file SHALL exist at `openspec/specs/geometry-types/spec.md`
- **AND** the directory name SHALL be `geometry-types` (not `geometry_types` or `GeometryTypes`)

#### Scenario: Spec file uses correct filename
- **WHEN** any capability spec is extracted
- **THEN** the spec file SHALL be named exactly `spec.md` (not `spec.yaml`, `README.md`, or anything else)

#### Scenario: No extra files in spec directory
- **WHEN** a capability spec is extracted
- **THEN** the spec directory SHALL contain only `spec.md` unless supplementary diagrams or data are explicitly required
- **AND** supplementary files, if any, SHALL be referenced from `spec.md`

### Requirement: Spec document structure
Every extracted `spec.md` SHALL follow the three-section structure established by existing specs: Purpose, Requirements, and Scenarios within Requirements.

#### Scenario: Purpose section present and concise
- **GIVEN** any extracted `spec.md`
- **WHEN** the document is parsed
- **THEN** it SHALL begin with a `## Purpose` heading followed by 1-3 paragraphs describing the capability scope

#### Scenario: Requirements use SHALL language
- **GIVEN** any extracted `spec.md`
- **WHEN** a requirement is defined
- **THEN** it SHALL be introduced with a `### Requirement:` heading and the body SHALL contain at least one sentence using "SHALL" to express the behavioral contract

#### Scenario: Scenarios use GIVEN/WHEN/THEN
- **GIVEN** any extracted `spec.md`
- **WHEN** a scenario is defined under a requirement
- **THEN** it SHALL be introduced with a `#### Scenario:` heading
- **AND** it SHALL contain at least **WHEN** and **THEN** clauses (GIVEN is optional when context is implicit)
- **AND** each clause SHALL be on its own line prefixed with `- **GIVEN**`, `- **WHEN**`, `- **THEN**`, or `- **AND**`

### Requirement: Minimum scenario coverage
Every requirement in an extracted spec SHALL have at least 3 scenarios to ensure adequate behavioral coverage.

#### Scenario: Happy path covered
- **GIVEN** a requirement for ST_Distance behavior
- **WHEN** scenarios are written
- **THEN** at least one scenario SHALL demonstrate the normal/expected usage with valid inputs and a concrete expected result

#### Scenario: Edge case covered
- **GIVEN** a requirement for ST_Distance behavior
- **WHEN** scenarios are written
- **THEN** at least one scenario SHALL cover an edge case such as: NULL input, empty geometry input, identical points (distance = 0), or SRID 0 (unknown)

#### Scenario: Error or boundary case covered
- **GIVEN** a requirement for ST_Distance behavior
- **WHEN** scenarios are written
- **THEN** at least one scenario SHALL cover an error condition or boundary such as: mixed SRIDs producing an error, or collection input requiring special dispatch

### Requirement: Concrete values in scenarios
Scenarios SHALL use concrete geometry values in WKT, EWKT, or hex WKB format and concrete expected output values, not abstract descriptions.

#### Scenario: WKT geometry in scenario
- **GIVEN** a scenario testing ST_Area on a polygon
- **WHEN** the scenario specifies the input
- **THEN** it SHALL use a concrete WKT string such as `POLYGON((0 0, 10 0, 10 10, 0 10, 0 0))` with expected result `100`
- **AND** it SHALL NOT use abstract language like "a polygon with some area"

#### Scenario: SRID specified explicitly
- **GIVEN** a scenario where SRID matters (e.g., ST_Transform, geography operations)
- **WHEN** the scenario specifies the input
- **THEN** it SHALL include the SRID in the geometry string (e.g., `SRID=4326;POINT(-73.9857 40.7484)`) or in a `ST_SetSRID` call

#### Scenario: Expected output is numeric or textual, not approximate
- **GIVEN** a scenario with a numeric result
- **WHEN** the expected value involves floating point
- **THEN** the scenario SHALL specify the precision tolerance (e.g., "within 0.001 meters") or the expected value to a defined number of decimal places

### Requirement: NULL propagation documentation
Every spec covering SQL-level functions SHALL document NULL input behavior for each function or group of functions.

#### Scenario: NULL geometry input
- **GIVEN** a spec covering ST_Intersects
- **WHEN** NULL handling is documented
- **THEN** there SHALL be a scenario showing that `ST_Intersects(NULL, geom)` returns NULL
- **AND** `ST_Intersects(geom, NULL)` returns NULL

#### Scenario: NULL both inputs
- **GIVEN** a spec covering a two-argument spatial function
- **WHEN** both arguments are NULL
- **THEN** the scenario SHALL document that the result is NULL (standard SQL NULL propagation)

#### Scenario: NULL in aggregate context
- **GIVEN** a spec covering an aggregate function (e.g., ST_Union aggregate)
- **WHEN** some inputs in the aggregate are NULL
- **THEN** the scenario SHALL document whether NULLs are skipped (standard aggregate behavior) or cause the result to be NULL

### Requirement: Empty geometry handling
Every spec covering spatial functions SHALL document behavior when input is an empty geometry (e.g., `GEOMETRYCOLLECTION EMPTY` or `POINT EMPTY`).

#### Scenario: Predicate with empty input
- **GIVEN** a spec covering spatial predicates
- **WHEN** empty geometry behavior is documented
- **THEN** there SHALL be a scenario showing that `ST_Intersects('POINT EMPTY', 'POINT(0 0)')` returns FALSE (empty geometries do not intersect anything)

#### Scenario: Measurement with empty input
- **GIVEN** a spec covering measurement functions
- **WHEN** empty geometry behavior is documented
- **THEN** there SHALL be a scenario for `ST_Area('POLYGON EMPTY')` returning 0 or NULL (depending on the function)

#### Scenario: Constructor with empty input
- **GIVEN** a spec covering constructors
- **WHEN** empty geometry behavior is documented
- **THEN** there SHALL be a scenario showing how `ST_Collect` handles empty geometry inputs in the collection

### Requirement: Test cross-reference
Each scenario SHALL include a reference to the regression test file that validates the described behavior, or SHALL be explicitly flagged as untested.

#### Scenario: Tested scenario references test file
- **GIVEN** a scenario for ST_Buffer with quad_segs parameter
- **WHEN** a corresponding regression test exists
- **THEN** the scenario SHALL include a note such as: `Validated by: regress/core/regress_buffer_params.sql`

#### Scenario: Untested scenario flagged
- **GIVEN** a scenario describing edge-case behavior not covered by existing tests
- **WHEN** no regression test validates the behavior
- **THEN** the scenario SHALL include a note: `Status: untested -- no existing regression test covers this case`

#### Scenario: CUnit test reference format
- **GIVEN** a scenario validated by a CUnit test rather than a SQL regression test
- **WHEN** the reference is written
- **THEN** it SHALL use the format: `Validated by: liblwgeom/cunit/cu_<suite>.c (<test_function_name>)`

### Requirement: Version guard documentation
Requirements that depend on specific library versions SHALL document the version guard macro and the minimum version.

#### Scenario: GEOS version-gated function
- **GIVEN** a requirement for ST_ConcaveHull (requires GEOS 3.11+)
- **WHEN** the version dependency is documented
- **THEN** the requirement SHALL include: "Requires `POSTGIS_GEOS_VERSION >= 31100`"
- **AND** the scenario SHALL note that on older GEOS versions, the function raises a "requires GEOS >= 3.11" error

#### Scenario: PostgreSQL version-gated behavior
- **GIVEN** a requirement that uses a PostgreSQL 14+ feature
- **WHEN** the version dependency is documented
- **THEN** the requirement SHALL include: "Requires `POSTGIS_PGSQL_VERSION >= 140`"

#### Scenario: No version guard needed
- **GIVEN** a requirement that works with all supported dependency versions
- **WHEN** the requirement is written
- **THEN** no version guard note is needed and none SHALL be added

### Requirement: Cross-reference between specs
When one spec references behavior defined in another spec, it SHALL use the spec directory name as the reference identifier.

#### Scenario: Geography spec references geometry-types
- **GIVEN** the `geography-type` spec describes casting from geometry to geography
- **WHEN** it references the geometry type system
- **THEN** it SHALL use the phrase "See the `geometry-types` spec" with the directory name in backticks

#### Scenario: Spatial predicates spec references spatial indexing
- **GIVEN** the `spatial-predicates` spec describes index-accelerated predicate execution
- **WHEN** it references the GiST operator class
- **THEN** it SHALL use the phrase "See the `spatial-indexing` spec for GiST operator class details"

#### Scenario: No circular primary ownership
- **GIVEN** two specs that reference each other
- **WHEN** the cross-references are written
- **THEN** each behavioral requirement SHALL have a single primary spec that owns it
- **AND** the other spec SHALL reference it rather than re-defining the same requirement

### Requirement: Naming conventions for requirements
Requirement headings SHALL be descriptive noun phrases that identify the behavioral contract, not imperative sentences or function names.

#### Scenario: Good requirement name
- **WHEN** a requirement is named
- **THEN** it SHALL use a descriptive phrase like "Spatial predicate NULL propagation" or "WKT round-trip fidelity for 3D geometries"
- **AND** it SHALL NOT be named after a single function like "ST_Intersects" (since multiple functions may share the behavior)

#### Scenario: Grouping related functions
- **GIVEN** a set of related functions (e.g., ST_Force2D, ST_Force3DZ, ST_Force3DM, ST_Force4D)
- **WHEN** they share the same behavioral contract pattern
- **THEN** they SHALL be covered by a single requirement (e.g., "Dimension coercion functions") with scenarios for each variant

#### Scenario: Single-function requirement when behavior is unique
- **GIVEN** a function with unique behavior not shared by others (e.g., ST_Buffer with style parameters)
- **WHEN** the requirement is written
- **THEN** it MAY use the function name in the requirement heading (e.g., "ST_Buffer style parameters and endcap/join options")

### Requirement: Extraction completeness tracking
Each extracted spec SHALL include a coverage summary at the end of the document listing the SQL functions covered and any known gaps.

#### Scenario: Coverage summary lists covered functions
- **GIVEN** the `spatial-predicates` spec
- **WHEN** the coverage summary is written
- **THEN** it SHALL list all SQL functions covered by the spec's requirements (e.g., ST_Intersects, ST_Contains, ST_Within, etc.)

#### Scenario: Coverage summary notes uncovered functions
- **GIVEN** a spec where some related functions are intentionally deferred
- **WHEN** the coverage summary is written
- **THEN** it SHALL list the deferred functions and note which spec will cover them or that they are out of scope

#### Scenario: Coverage summary notes test coverage percentage
- **GIVEN** a completed spec
- **WHEN** the coverage summary is written
- **THEN** it SHALL note how many of the spec's scenarios are validated by existing regression tests vs flagged as untested
