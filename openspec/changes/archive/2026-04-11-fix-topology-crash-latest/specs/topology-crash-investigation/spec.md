## ADDED Requirements

### Requirement: Latest Docker image CI must not block PRs
The `latest` Docker image uses bleeding-edge PostgreSQL and GEOS which may have
compatibility issues. CI jobs using this image must not prevent PR merges.

#### Scenario: Latest tests fail due to upstream bug
- **GIVEN** the `latest` Docker image has a PostgreSQL/GEOS compatibility bug
- **WHEN** the `droptopogeometrycolumn` test crashes the backend
- **THEN** the CI job reports failure but does not block the PR (continue-on-error)

#### Scenario: Latest tests pass after upstream fix
- **GIVEN** upstream PostGIS fixes the topology crash
- **WHEN** the `latest` Docker image is updated with the fix
- **THEN** the CI job passes and continue-on-error can optionally be removed

#### Scenario: Version-pinned CI remains blocking
- **GIVEN** version-pinned CI jobs (pg14-18) use stable releases
- **WHEN** any version-pinned job fails
- **THEN** the failure blocks the PR (continue-on-error is false)
