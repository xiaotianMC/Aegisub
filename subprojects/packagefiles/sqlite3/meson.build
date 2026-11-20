project('sqlite3', 'c', version: '3.54.1')

sqlite = static_library('sqlite3', 'sqlite3.c',
    c_args: ['-DSQLITE_OMIT_LOAD_EXTENSION', '-DSQLITE_DQS=0'],
    dependencies: dependency('threads'),
    override_options: ['warning_level=0'],
)
sqlite3_dep = declare_dependency(link_with: sqlite, include_directories: include_directories('.'))
