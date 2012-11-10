#!/usr/bin/env python

from subprocess import Popen, PIPE, STDOUT
from os.path import basename

def conf_get_git_rev():
    """
    Get the SHA1 of git HEAD
    """
    devnull = open('/dev/null', 'w')
    p = Popen(['git', 'describe'], stdout=PIPE, stderr=devnull)
    stdout = p.communicate()[0]
    if p.returncode == 0:
        return stdout.strip()
    p = Popen(['git', 'rev-parse', 'HEAD'], stdout=PIPE, \
                  stderr=devnull)
    stdout = p.communicate()[0]

    if p.returncode == 0:
        return stdout.strip()[:8]
    else:
        return "%s %d "%(stdout, p.returncode)
    return 'norevision'

top = '.'
out = 'bin'
APPNAME='koinode'

_sources = [
    'settings.cpp',
    'sha1.cpp',
    'emitter.cpp',
    'nexus.cpp',
    'runner.cpp',
    'msg.cpp',
    'servicemgr.cpp',
    'crypt.cpp',
    'elector.cpp',
    'logging.cpp',
    'cmd.cpp',
    'os.cpp',
    'cluster.cpp',
    'clusterstate.cpp',
    'masterstate.cpp',
    'miniz.c',
    'network.cpp',
    'file.cpp'
    ]
cxxflags = ['-g',
            '-O2',
            '-std=c++11',
            '-Wall',
            '-Wextra',
            '-Werror',
            '-Wwrite-strings',
            '-Wno-unused',
            '-Wno-unused-parameter',
            '-DDEBUG=1']
ldflags = ['-g', '-std=c++11']

def options(opt):
    opt.load(['compiler_c', 'compiler_cxx', 'waf_unit_test', 'boost'])

def configure(conf):
    conf.check_waf_version(mini='1.7.5')
    conf.load(['compiler_c', 'compiler_cxx', 'waf_unit_test', 'boost'])
    conf.check(header_name=['sys/types.h', 'sys/wait.h'], features='cxx cxxprogram', mandatory=True)
    conf.check_boost(lib=['system', 'regex', 'program_options', 'date_time'], mt=True, static=False)

def _tests(bld):
    import os, glob

    testfiles = glob.glob(os.path.join('test', '*_test.cpp'))

    bld.program(
        source = testfiles + ['test/test.cpp'],
        target = 'tests',
        includes = ['.', 'src', 'catch/include'],
        features = 'test',
        cxxflags=cxxflags + ['-Wno-shadow'],
        linkflags = ldflags,
        lib = ['pthread'],
        install_path = None,
        use='common_objects BOOST')

def build(bld):
    sources = ['src/'+s for s in _sources]
    bld.objects(source=sources,
                cxxflags=cxxflags + ['-Wshadow'],
                target='common_objects',
                includes = '.',
                use="BOOST")

    _tests(bld)

    bld(
        features = 'subst',
        source = 'scripts/version.template',
        target = 'version.c',
        REVISION = conf_get_git_rev())

    bld.program(
        source=['src/koi.cpp', 'src/run.cpp', 'version.c'],
        target='koinode',
        lib = ['pthread'],
        cxxflags=cxxflags + ['-Wshadow', '-Wswitch-default'],
        linkflags=ldflags,
        use='common_objects BOOST',
        install_path = '${PREFIX}/sbin',
        includes = '.')

    bld.program(
        source=['src/cli.cpp', 'version.c'],
        target='koi',
        lib = ['pthread'],
        cxxflags=cxxflags + ['-Wshadow', '-Wswitch-default'],
        linkflags=ldflags,
        use='common_objects BOOST',
        install_path = '${PREFIX}/sbin',
        includes = '.')

    from waflib.Tools import waf_unit_test
    bld.add_post_fun(test_summary)

    bld.install_files('${PREFIX}/share/koi', 'scripts/koi-service.sh')
    bld.install_files('${DESTDIR}/etc/koi', 'configs/example.conf')

    import os
    if os.path.exists('doc/koinode.1'):
        bld.install_files('${PREFIX}/share/man/man1/', 'doc/koinode.1')
    if os.path.exists('doc/koi.1'):
        bld.install_files('${PREFIX}/share/man/man1/', 'doc/koi.1')

def lastline(s):
    try:
        return [l for l in s.splitlines() if len(l)][-1]
    except:
        return ''

def test_summary(bld):
    from waflib import Logs
    lst = getattr(bld, 'utest_results', [])
    if lst:
        total = len(lst)
        tfail = len([x for x in lst if x[1]])

        Logs.pprint('CYAN', 'Pass: %d/%d' % (total-tfail, total))
        for (f, code, out, err) in lst:
            if not code:
                ll = lastline(out)
                Logs.pprint('CYAN', '    %s: %s' % (basename(f), ll))

        Logs.pprint('CYAN', 'Fail: %d/%d' % (tfail, total))
        for (f, code, out, err) in lst:
            if code:
                Logs.pprint('RED',   '  %s (%s)' % (basename(f), code))
                if len(err):
                    Logs.pprint('NORMAL', '%s' % (err))
                if len(out):
                    Logs.pprint('NORMAL', '%s' % (out))
