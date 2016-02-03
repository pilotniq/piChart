describe('verification of standard cases', function() {

  it('should parse DMS coordinates in Copenhagen', function() {
    var latitude = Geo.parseDMS('55 40 34N');
    expect((latitude).toFixed(4)).toBe('55.6761');
    var longitude = Geo.parseDMS('12 34 06E');
    expect((longitude).toFixed(4)).toBe('12.5683');
  });

  it('should parse DMS coordinates in San Francisco', function() {
    var latitude = Geo.parseDMS('34 47 0N');
    expect((latitude).toFixed(4)).toBe('34.7833');
    var longitude = Geo.parseDMS('122 25 0W');
    expect((longitude).toFixed(4)).toBe('-122.4167');
  });

  it('should parse DMS coordinates in Lima', function() {
    var latitude = Geo.parseDMS('12 2 36S');
    expect((latitude).toFixed(4)).toBe('-12.0433');
    var longitude = Geo.parseDMS('77 1 42W');
    expect((longitude).toFixed(4)).toBe('-77.0283');
  });

  it('should parse DMS coordinates in Darwin', function() {
    var latitude = Geo.parseDMS('12 27 0S');
    expect(latitude).toBe(-12.45);
    var longitude = Geo.parseDMS('130 50 0E');
    expect((longitude).toFixed(4)).toBe('130.8333');
  });

  it('should convert decimal degrees coordinates in Copenhagen to DMS', function() {
    var latitude = Geo.toLat(55.6761);
    expect(latitude).toBe('55°40′34″N');
    var longitude = Geo.toLon(12.5683);
    expect(longitude).toBe('012°34′06″E');
  });

  it('should convert decimal degrees coordinates in San Francisco to DMS', function() {
    var latitude = Geo.toLat(34.7833);
    expect(latitude).toBe('34°47′00″N');
    var longitude = Geo.toLon(-122.4167);
    expect(longitude).toBe('122°25′00″W');
  });

  it('should convert decimal degrees coordinates in Lima to DMS', function() {
    var latitude = Geo.toLat(-12.0433);
    expect(latitude).toBe('12°02′36″S');
    var longitude = Geo.toLon(-77.0283);
    expect(longitude).toBe('077°01′42″W');
  });

  it('should convert decimal degrees coordinates in Darwin to DMS', function() {
    var latitude = Geo.toLat(-12.45);
    expect(latitude).toBe('12°27′00″S');
    var longitude = Geo.toLon(130.8333);
    expect(longitude).toBe('130°50′00″E');
  });
});
