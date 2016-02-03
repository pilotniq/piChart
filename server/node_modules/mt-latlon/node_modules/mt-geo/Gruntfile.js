module.exports = function(grunt) {

  'use strict';

  [
    'grunt-contrib-jasmine'
  ].forEach(grunt.loadNpmTasks);

  grunt.initConfig({
    // Unit tests.
    jasmine: {
      src: [
        'mt-geo.js'
      ],
      options: {
        specs: ['specs/**/*.js'],
        junit : {
            path : 'specs/output',
            consolidate : true
        }
      }
    },
  });

  grunt.registerTask('test', ['jasmine']);

  grunt.registerTask('default', ['test']);
};
