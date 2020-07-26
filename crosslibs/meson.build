d3d12_spec = files('d3d12.spec')

d3d12_def = custom_target('d3d12_def_target',
    input             : d3d12_spec,
    output            : '@BASENAME@.def',
    build_by_default  : true,
    install           : false,
    command           : [ def_compiler, vkd3d_arch_arg, '-w', '--def',
                        '--export', '@INPUT@',
                        '-o', '@OUTPUT@' ],)
