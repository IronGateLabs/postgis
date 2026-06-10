## Purpose

Defines the PostGIS Raster data model and SQL API as implemented in the `postgis_raster` extension. This covers the raster type structure (pixel grid with georeference, multi-band model, pixel types, nodata), raster construction, pixel value access and modification, map algebra operations (expression-based and callback-based), raster clipping by geometry, raster union/mosaic aggregation, resampling/rescaling, geometry-to-raster conversion, raster-vector spatial predicates, and GDAL driver integration for import/export. The raster model uses a 6-parameter affine geotransform (upper-left X/Y, scale X/Y, skew X/Y) plus SRID for spatial referencing.

## ADDED Requirements

### Requirement: Raster data model and pixel types
The system SHALL define a raster type consisting of: width (uint16), height (uint16), SRID (int32), a 6-parameter affine geotransform (upper-left X, upper-left Y, scale X, scale Y, skew X, skew Y), and zero or more bands. Each band SHALL have a pixel type from the enumeration:

| Constant | Value | Description |
|---|---|---|
| PT_1BB | 0 | 1-bit boolean |
| PT_2BUI | 1 | 2-bit unsigned integer |
| PT_4BUI | 2 | 4-bit unsigned integer |
| PT_8BSI | 3 | 8-bit signed integer |
| PT_8BUI | 4 | 8-bit unsigned integer |
| PT_16BSI | 5 | 16-bit signed integer |
| PT_16BUI | 6 | 16-bit unsigned integer |
| PT_32BSI | 7 | 32-bit signed integer |
| PT_32BUI | 8 | 32-bit unsigned integer |
| PT_16BF | 9 | 16-bit float |
| PT_32BF | 10 | 32-bit float |
| PT_64BF | 11 | 64-bit float |

Each band optionally has a nodata value and an isnodata flag. Bands can be in-database (inline pixel data) or out-of-database (referencing an external file via path and band number).

#### Scenario: Raster with 8BUI band stores pixel values correctly
- **GIVEN** `SELECT ST_MakeEmptyRaster(2, 2, 0, 0, 1)` creates a 2x2 raster
- **WHEN** an 8BUI band is added with `ST_AddBand(rast, '8BUI', 0)`
- **THEN** `ST_BandPixelType(rast, 1)` SHALL return '8BUI'
- **AND** pixel values SHALL be clamped to the range 0-255
- Validated by: raster/test/regress/rt_band_properties.sql

#### Scenario: All pixel types are supported
- **WHEN** bands of each pixel type (1BB through 64BF) are created
- **THEN** `ST_BandPixelType` SHALL return the correct type name for each
- **AND** `ST_Value` SHALL correctly read/write values within each type's range
- Validated by: raster/test/regress/rt_band_properties.sql

#### Scenario: Out-of-database band references external file
- **GIVEN** a raster with an out-of-db band pointing to a GDAL-readable file
- **WHEN** `ST_BandIsNoData` and `ST_Value` are called
- **THEN** pixel values SHALL be read from the external file via GDAL
- **AND** `ST_BandPath(rast, 1)` SHALL return the file path
- Validated by: raster/test/regress/rt_band.sql

### Requirement: Raster spatial reference and geotransform
Each raster SHALL carry a 6-parameter affine geotransform defining the mapping from pixel coordinates (column, row) to world coordinates: `Xworld = scaleX * col + skewX * row + upperLeftX` and `Yworld = skewX * col + scaleY * row + upperLeftY`. The raster also carries an SRID. Functions `ST_UpperLeftX/Y`, `ST_ScaleX/Y`, `ST_SkewX/Y`, `ST_Width`, `ST_Height`, `ST_SRID`, `ST_NumBands` SHALL expose these properties.

#### Scenario: Geotransform properties accessible via SQL
- **GIVEN** `ST_MakeEmptyRaster(10, 20, 100.5, 200.5, 0.5, -0.5, 0.1, -0.1, 4326)`
- **WHEN** property functions are called
- **THEN** ST_Width=10, ST_Height=20, ST_UpperLeftX=100.5, ST_UpperLeftY=200.5, ST_ScaleX=0.5, ST_ScaleY=-0.5, ST_SkewX=0.1, ST_SkewY=-0.1, ST_SRID=4326
- Validated by: raster/test/regress/rt_metadata.sql

#### Scenario: ST_RasterToWorldCoord converts pixel to world coordinates
- **GIVEN** a raster with known geotransform
- **WHEN** `ST_RasterToWorldCoord(rast, col, row)` is called
- **THEN** the returned X and Y SHALL match the affine transform computation
- Validated by: raster/test/regress/rt_rastertoworldcoord.sql

#### Scenario: ST_WorldToRasterCoord converts world to pixel coordinates
- **GIVEN** a raster with known geotransform
- **WHEN** `ST_WorldToRasterCoord(rast, x, y)` is called
- **THEN** the returned column and row SHALL be the inverse of the affine transform
- Validated by: raster/test/regress/rt_worldtorastercoord.sql

### Requirement: ST_MakeEmptyRaster constructs empty raster
`ST_MakeEmptyRaster(width, height, upperleftx, upperlefty, scalex, scaley, skewx, skewy, srid)` SHALL create a raster with the specified dimensions and geotransform but no bands. An overload `ST_MakeEmptyRaster(width, height, upperleftx, upperlefty, pixelsize)` uses equal scale and zero skew. An overload `ST_MakeEmptyRaster(raster)` copies the geotransform from an existing raster.

#### Scenario: Create empty raster with full parameters
- **WHEN** `ST_MakeEmptyRaster(100, 100, 0, 0, 1, -1, 0, 0, 4326)` is called
- **THEN** the result SHALL have width=100, height=100, 0 bands, SRID=4326
- Validated by: raster/test/regress/rt_utility.sql

#### Scenario: Create empty raster from existing raster
- **GIVEN** a raster with specific geotransform and SRID
- **WHEN** `ST_MakeEmptyRaster(existing_rast)` is called
- **THEN** the new raster SHALL have the same geotransform and SRID but 0 bands
- Validated by: raster/test/regress/rt_utility.sql

#### Scenario: Create empty raster with simple pixel size
- **WHEN** `ST_MakeEmptyRaster(10, 10, 0, 10, 1.0)` is called
- **THEN** scaleX=1.0, scaleY=-1.0, skewX=0, skewY=0
- Validated by: raster/test/regress/rt_utility.sql

### Requirement: ST_AddBand adds bands to a raster
`ST_AddBand(raster, pixeltype, initialvalue, nodataval)` SHALL add a new band to the raster with the specified pixel type, filling all pixels with the initial value, and optionally setting the nodata value. Multiple overloads exist for adding bands from other rasters, from out-of-db sources, or using `addbandarg` arrays.

#### Scenario: Add single band with initial value
- **GIVEN** an empty 3x3 raster
- **WHEN** `ST_AddBand(rast, '32BF', 42.5, -9999)` is called
- **THEN** `ST_NumBands(rast)` SHALL return 1
- **AND** `ST_Value(rast, 1, 1, 1)` SHALL return 42.5
- **AND** `ST_BandNoDataValue(rast, 1)` SHALL return -9999
- Validated by: raster/test/regress/rt_addband.sql

#### Scenario: Add multiple bands with addbandarg array
- **GIVEN** an empty raster
- **WHEN** ST_AddBand is called with an array of addbandarg values for 3 bands
- **THEN** `ST_NumBands(rast)` SHALL return 3
- Validated by: raster/test/regress/rt_addband.sql

#### Scenario: Add band from another raster
- **GIVEN** two rasters with compatible dimensions
- **WHEN** `ST_AddBand(rast1, rast2, band_num)` is called
- **THEN** the specified band from rast2 SHALL be copied to rast1
- Validated by: raster/test/regress/rt_addband.sql

### Requirement: ST_Value reads pixel values
`ST_Value(raster, band, x, y, exclude_nodata)` SHALL return the pixel value at the given column/row position. An overload accepts a point geometry and uses the geotransform to find the pixel. When `exclude_nodata` is true (default), pixels at the nodata value return NULL. A `resample` parameter allows bilinear interpolation when reading by point geometry.

#### Scenario: Read pixel value by column/row
- **GIVEN** a raster with band 1 containing known values
- **WHEN** `ST_Value(rast, 1, 2, 3)` is called
- **THEN** the value at column 2, row 3 SHALL be returned
- Validated by: raster/test/regress/rt_pixelvalue.sql

#### Scenario: Read pixel value by point geometry
- **GIVEN** a raster with a geotransform mapping POINT(0.5, -0.5) to column 1, row 1
- **WHEN** `ST_Value(rast, 1, 'POINT(0.5 -0.5)')` is called
- **THEN** the value at column 1, row 1 SHALL be returned
- Validated by: raster/test/regress/rt_pixelvalue.sql

#### Scenario: Nodata pixels return NULL when excluded
- **GIVEN** a raster with nodata=0 and pixel at (1,1) set to 0
- **WHEN** `ST_Value(rast, 1, 1, 1, true)` is called
- **THEN** NULL SHALL be returned
- **AND** `ST_Value(rast, 1, 1, 1, false)` SHALL return 0
- Validated by: raster/test/regress/rt_pixelvalue.sql

### Requirement: ST_SetValue and ST_SetValues modify pixel values
`ST_SetValue(raster, band, x, y, newvalue)` SHALL return a new raster with the pixel at (x,y) set to the new value. `ST_SetValues` provides batch updates by geometry or array of values.

#### Scenario: Set single pixel value
- **GIVEN** a raster with band 1
- **WHEN** `ST_SetValue(rast, 1, 2, 2, 99.0)` is called
- **THEN** the returned raster's pixel at (2,2) SHALL have value 99.0
- **AND** other pixels SHALL remain unchanged
- Validated by: raster/test/regress/rt_set_band_properties.sql

#### Scenario: Set values by geometry mask
- **GIVEN** a raster and a polygon geometry
- **WHEN** `ST_SetValues(rast, 1, geom, newval)` is called
- **THEN** all pixels intersecting the geometry SHALL be set to newval
- Validated by: raster/test/regress/rt_setvalues_geomval.sql

#### Scenario: Set values by array
- **GIVEN** a raster
- **WHEN** `ST_SetValues(rast, 1, x, y, array_of_values)` is called
- **THEN** the rectangular block of pixels starting at (x,y) SHALL be set to the array values
- Validated by: raster/test/regress/rt_setvalues_array.sql

### Requirement: ST_MapAlgebra performs raster algebra
`ST_MapAlgebra` SHALL support both expression-based and callback-based forms for single-raster and two-raster operations. The expression form accepts a SQL expression string with `[rast]` or `[rast.val]` placeholders. The callback form accepts a user-defined function (regprocedure). The result raster pixel type, nodata value, and extent type (INTERSECTION, UNION, FIRST, SECOND) are configurable.

#### Scenario: Single-raster expression map algebra
- **GIVEN** a raster with integer pixel values
- **WHEN** `ST_MapAlgebra(rast, 1, '32BF', '[rast.val] * 2')` is called
- **THEN** each pixel in the output SHALL be double the input value
- Validated by: raster/test/regress/rt_mapalgebraexpr.sql

#### Scenario: Two-raster expression map algebra
- **GIVEN** two aligned rasters rast1 and rast2
- **WHEN** `ST_MapAlgebra(rast1, 1, rast2, 1, '[rast1.val] + [rast2.val]', '32BF', 'INTERSECTION')` is called
- **THEN** each output pixel SHALL be the sum of corresponding input pixels
- **AND** the output extent SHALL be the intersection of the two inputs
- Validated by: raster/test/regress/rt_mapalgebraexpr_2raster.sql

#### Scenario: Callback map algebra with user function
- **GIVEN** a raster and a user-defined PL/pgSQL function
- **WHEN** `ST_MapAlgebra(rast, 1, callback_function)` is called
- **THEN** the callback function SHALL be invoked for each pixel
- **AND** the return value SHALL become the output pixel value
- Validated by: raster/test/regress/rt_mapalgebrafct.sql

#### Scenario: Multi-band map algebra via new ST_MapAlgebra API
- **GIVEN** multiple rasters
- **WHEN** the n-raster ST_MapAlgebra callback form is used
- **THEN** the user function SHALL receive pixel values from all input rasters
- Validated by: raster/test/regress/rt_mapalgebra.sql

### Requirement: ST_Clip clips raster by geometry
`ST_Clip(raster, band, geometry, nodataval, crop)` SHALL return a raster clipped to the given geometry. Pixels outside the geometry SHALL be set to nodataval (or the band's nodata if not specified). When `crop` is true, the output raster extent SHALL be reduced to the geometry's bounding box.

#### Scenario: Clip raster to polygon
- **GIVEN** a 10x10 raster and a polygon covering the upper-left quadrant
- **WHEN** `ST_Clip(rast, geom, true)` is called with crop=true
- **THEN** the output raster SHALL have reduced dimensions matching the polygon's extent
- **AND** pixels outside the polygon SHALL be nodata
- Validated by: raster/test/regress/rt_clip.sql

#### Scenario: Clip raster without cropping
- **GIVEN** the same raster and polygon
- **WHEN** `ST_Clip(rast, geom, -9999, false)` is called with crop=false
- **THEN** the output raster SHALL keep the original dimensions
- **AND** pixels outside the polygon SHALL have value -9999
- Validated by: raster/test/regress/rt_clip.sql

#### Scenario: Clip with multi-band raster
- **GIVEN** a raster with 3 bands
- **WHEN** ST_Clip is called without specifying a band
- **THEN** all bands SHALL be clipped
- Validated by: raster/test/regress/rt_clip.sql

### Requirement: ST_Union aggregates rasters into a mosaic
`ST_Union` is an aggregate function that merges multiple rasters into a single output raster covering the extent of all inputs. When pixels overlap, the union type determines the result: LAST (default), FIRST, MIN, MAX, MEAN, RANGE, SUM, COUNT.

#### Scenario: Union of non-overlapping tiles
- **GIVEN** four non-overlapping quarter tiles
- **WHEN** `SELECT ST_Union(rast) FROM tiles` is executed
- **THEN** the output raster SHALL cover the combined extent
- **AND** each pixel SHALL match its source tile value
- Validated by: raster/test/regress/rt_union.sql

#### Scenario: Union with overlapping rasters using MAX
- **GIVEN** two overlapping rasters
- **WHEN** `SELECT ST_Union(rast, 'MAX')` is executed
- **THEN** overlapping pixels SHALL have the maximum value from the inputs
- Validated by: raster/test/regress/rt_union.sql

#### Scenario: Union with MEAN aggregation
- **GIVEN** overlapping rasters with known values
- **WHEN** `SELECT ST_Union(rast, 'MEAN')` is executed
- **THEN** overlapping pixels SHALL have the mean of input values
- Validated by: raster/test/regress/rt_union.sql

### Requirement: Raster resampling operations
`ST_Resample(raster, width, height, ...)` SHALL resample a raster to new dimensions using a specified algorithm (NearestNeighbour, Bilinear, Cubic, CubicSpline, Lanczos). `ST_Rescale(raster, scalex, scaley)` resamples to a new pixel scale. `ST_Reskew(raster, skewx, skewy)` adjusts skew. All use GDAL's warping infrastructure.

#### Scenario: Rescale raster to coarser resolution
- **GIVEN** a raster with scaleX=1, scaleY=-1
- **WHEN** `ST_Rescale(rast, 2, -2)` is called
- **THEN** the output raster SHALL have scaleX=2, scaleY=-2
- **AND** width and height SHALL be approximately halved
- Validated by: raster/test/regress/rt_gdalwarp.sql

#### Scenario: Resample with bilinear interpolation
- **GIVEN** a raster with smooth value gradients
- **WHEN** `ST_Resample(rast, 200, 200, algorithm => 'Bilinear')` is called
- **THEN** the output SHALL have 200x200 pixels with interpolated values
- Validated by: raster/test/regress/rt_gdalwarp.sql

#### Scenario: Reskew applies new skew values
- **GIVEN** a raster with zero skew
- **WHEN** `ST_Reskew(rast, 0.1, 0.1)` is called
- **THEN** the output raster SHALL have skewX=0.1, skewY=0.1
- Validated by: raster/test/regress/rt_gdalwarp.sql

### Requirement: ST_AsRaster converts geometry to raster
`ST_AsRaster(geometry, raster_template_or_dimensions, pixeltype, value, nodataval)` SHALL rasterize a geometry onto a grid defined by either a reference raster or explicit dimensions. Pixels touched by the geometry receive the specified value; others receive nodata.

#### Scenario: Rasterize polygon onto reference grid
- **GIVEN** a polygon geometry and a reference raster defining the grid
- **WHEN** `ST_AsRaster(geom, ref_rast, '8BUI', 1, 0)` is called
- **THEN** pixels inside the polygon SHALL have value 1
- **AND** pixels outside SHALL have value 0 (nodata)
- Validated by: raster/test/regress/rt_asraster.sql

#### Scenario: Rasterize with explicit dimensions
- **WHEN** `ST_AsRaster(geom, 100, 100, '32BF', 255)` is called
- **THEN** a 100x100 raster SHALL be produced covering the geometry's extent
- Validated by: raster/test/regress/rt_asraster.sql

#### Scenario: Rasterize line geometry
- **GIVEN** a linestring geometry
- **WHEN** ST_AsRaster is called
- **THEN** pixels along the line path SHALL receive the specified value
- Validated by: raster/test/regress/rt_asraster.sql

### Requirement: Raster-vector spatial predicates
`ST_Intersects(raster, geometry)` and `ST_Intersects(raster, raster)` SHALL test whether non-nodata pixels of a raster intersect a geometry or another raster. Related functions `ST_DWithin`, `ST_DFullyWithin`, `ST_Disjoint`, `ST_Contains`, `ST_Covers`, `ST_CoveredBy`, `ST_Overlaps`, `ST_Touches` provide additional spatial relationship tests between rasters and between rasters and geometries.

#### Scenario: Raster-geometry intersection test
- **GIVEN** a raster covering (0,0)-(10,10) and a point POINT(5 5)
- **WHEN** `ST_Intersects(rast, 'POINT(5 5)')` is called
- **THEN** true SHALL be returned (point falls within a non-nodata pixel)
- Validated by: raster/test/regress/rt_intersects.sql

#### Scenario: Raster-raster intersection test
- **GIVEN** two rasters with overlapping non-nodata pixels
- **WHEN** `ST_Intersects(rast1, rast2)` is called
- **THEN** true SHALL be returned
- Validated by: raster/test/regress/rt_geos_relationships.sql

#### Scenario: Non-intersecting raster and geometry
- **GIVEN** a raster covering (0,0)-(10,10) and a point POINT(100 100)
- **WHEN** `ST_Intersects(rast, 'POINT(100 100)')` is called
- **THEN** false SHALL be returned
- Validated by: raster/test/regress/rt_intersects.sql

### Requirement: Nodata handling
Each band MAY have a nodata value set via `ST_SetBandNoDataValue`. The `hasnodata` flag indicates whether a nodata value is defined. The `isnodata` flag indicates whether all pixels in the band equal the nodata value. When `exclude_nodata_value` is true (the default for most functions), nodata pixels are treated as absent/NULL.

#### Scenario: Set and get nodata value
- **GIVEN** a raster with a band
- **WHEN** `ST_SetBandNoDataValue(rast, 1, -9999)` is called
- **THEN** `ST_BandNoDataValue(rast, 1)` SHALL return -9999
- Validated by: raster/test/regress/rt_set_band_properties.sql

#### Scenario: BandIsNoData detects all-nodata band
- **GIVEN** a band where all pixels equal the nodata value
- **WHEN** `ST_BandIsNoData(rast, 1)` is called
- **THEN** true SHALL be returned
- Validated by: raster/test/regress/rt_band_properties.sql

#### Scenario: Nodata value clamped to pixel type range
- **GIVEN** a band of type 8BUI (range 0-255)
- **WHEN** nodata is set to 256
- **THEN** the value SHALL be clamped or an error raised
- Status: untested -- edge case for nodata clamping not directly tested

### Requirement: GDAL driver integration for I/O
`ST_AsGDALRaster(raster, format, options)` SHALL serialize a raster to the specified GDAL format (e.g., 'GTiff', 'PNG', 'JPEG') as a bytea value. `ST_FromGDALRaster(bytea, srid)` SHALL deserialize a GDAL-format byte array back into a raster. The available drivers are controlled by the `postgis.gdal_enabled_drivers` GUC setting.

#### Scenario: Export raster as GeoTIFF
- **GIVEN** a raster with one band
- **WHEN** `ST_AsGDALRaster(rast, 'GTiff')` is called
- **THEN** a non-empty bytea SHALL be returned containing valid GeoTIFF data
- Validated by: raster/test/regress/rt_astiff.sql

#### Scenario: Export and reimport round-trip
- **GIVEN** a raster with known pixel values
- **WHEN** exported as GeoTIFF and reimported with ST_FromGDALRaster
- **THEN** the reimported raster SHALL have the same dimensions, geotransform, and pixel values
- Validated by: raster/test/regress/rt_fromgdalraster.sql

#### Scenario: Disabled driver raises error
- **GIVEN** postgis.gdal_enabled_drivers does not include 'GTiff'
- **WHEN** `ST_AsGDALRaster(rast, 'GTiff')` is called
- **THEN** an error SHALL be raised indicating the driver is not enabled
- Validated by: raster/test/regress/permitted_gdal_drivers.sql

### Requirement: Raster WKB serialization
Rasters SHALL have a binary serialization format (WKB) used for storage in PostgreSQL and transport. The format encodes the raster header (endianness, version, nBands, scaleX/Y, ipX/Y, skewX/Y, srid, width, height), followed by band data (pixtype flags, nodata value, pixel data or out-of-db reference). Round-trip through WKB SHALL preserve all raster properties.

#### Scenario: Raster WKB round-trip preserves properties
- **GIVEN** a raster with 2 bands, SRID=4326, and specific geotransform
- **WHEN** serialized to hex WKB and deserialized back
- **THEN** all properties (dimensions, geotransform, SRID, bands, pixel values) SHALL match
- Validated by: raster/test/regress/rt_wkb.sql

#### Scenario: WKB output as hex string
- **GIVEN** a raster
- **WHEN** cast to text (hex WKB representation)
- **THEN** the output SHALL be a valid hex-encoded raster WKB
- Validated by: raster/test/regress/rt_bytea.sql

#### Scenario: Empty raster WKB (no bands)
- **GIVEN** `ST_MakeEmptyRaster(1, 1, 0, 0, 1)`
- **WHEN** serialized to WKB
- **THEN** the nBands field SHALL be 0 and no band data SHALL follow the header
- Validated by: raster/test/regress/rt_wkb.sql

### Requirement: Raster metadata and envelope
`ST_MetaData(raster)` SHALL return a composite row (upperleftx, upperlefty, width, height, scalex, scaley, skewx, skewy, srid, numbands). `ST_Envelope(raster)` SHALL return the bounding polygon of the raster in world coordinates. `ST_ConvexHull(raster)` SHALL return the convex hull accounting for skew.

#### Scenario: ST_MetaData returns all raster properties
- **GIVEN** a raster with known properties
- **WHEN** `ST_MetaData(rast)` is called
- **THEN** all 10 fields SHALL match the raster's properties
- Validated by: raster/test/regress/rt_metadata.sql

#### Scenario: ST_Envelope returns bounding rectangle
- **GIVEN** a raster with no skew
- **WHEN** `ST_Envelope(rast)` is called
- **THEN** the result SHALL be a POLYGON representing the raster's bounding rectangle
- Validated by: raster/test/regress/rt_envelope.sql

#### Scenario: ST_ConvexHull handles skewed raster
- **GIVEN** a raster with non-zero skew
- **WHEN** `ST_ConvexHull(rast)` is called
- **THEN** the result SHALL be a parallelogram reflecting the skew
- Validated by: raster/test/regress/rt_convexhull.sql
