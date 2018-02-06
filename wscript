#!/usr/bin/env python

from subprocess import Popen, PIPE
from os.path import basename
from waflib import Logs
import os
import sys


def _conf_get_git_rev():
    """
    Get the SHA1 of git HEAD
    """
    try:
        devnull = open('/dev/null', 'w')
        p = Popen(['git', 'describe'], stdout=PIPE, stderr=devnull)
        stdout = p.communicate()[0]
        if p.returncode == 0:
            return stdout.strip()
        p = Popen(['git', 'rev-parse', 'HEAD'], stdout=PIPE,
                  stderr=devnull)
        stdout = p.communicate()[0]

        if p.returncode == 0:
            return stdout.strip()[:8]
    except OSError:
        pass
    return 'none'

top = '.'
out = 'bin'
APPNAME = 'koinode'
_disable_tests = '--notests' in sys.argv

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
            '-Wall',
            '-Wextra',
            '-Werror',
            '-Wshadow',
            '-Wswitch-default',
            '-Wwrite-strings',
            '-Wno-error=shadow',
            '-Wno-unused',
            '-Wno-unused-parameter',
            '-DDEBUG=1']

ldflags = ['-g']

if sys.platform == 'darwin':
    cxxflags += ['-std=c++11', '-stdlib=libc++']
    ldflags += ['-std=c++11', '-stdlib=libc++']
else:
    cxxflags += ['-std=c++0x']
    ldflags += ['-std=c++0x']

sysincludes = []
if 'SYSINCLUDES' in os.environ:
    for si in os.environ['SYSINCLUDES'].split(','):
        sysincludes.append('-isystem')
        sysincludes.append(si)


def options(opt):
    opt.load(['compiler_c', 'compiler_cxx', 'waf_unit_test', 'boost'])


def configure(conf):
    conf.check_waf_version(mini='1.7.5')
    conf.load(['compiler_c', 'compiler_cxx', 'waf_unit_test', 'boost'])
    try:
        try:
            conf.check(header_name=['sys/types.h', 'sys/wait.h'],
                       cxxflags=cxxflags,
                       features='cxx cxxprogram',
                       mandatory=True)
        except conf.errors.WafError, e:
            Logs.warn('GCC 4.4+ or clang 3.1+ with C++0x/C++11 support required')
            raise e

        try:
            conf.check_boost(lib=['system', 'regex', 'program_options', 'date_time'],
                             mt=True, stlib=False)
        except conf.errors.WafError, e:
            Logs.warn('boost 1.49+ required: see http://boost.org')
            raise e

    except conf.errors.WafError, e:
        Logs.info('To specify an alternate compiler, use CC and CXX environment variables')
        raise e


def _tests(bld):
    import os
    import glob

    testfiles = glob.glob(os.path.join('test', '*_test.cpp'))

    bld.program(
        source=testfiles + ['test/test.cpp'],
        target='tests',
        includes=['.', 'src', 'catch/include'],
        features='test',
        cxxflags=cxxflags + ['-Wno-error=switch-default'],
        linkflags=ldflags,
        lib=['pthread'],
        install_path=None,
        use='common_objects BOOST')


def build(bld):
    sources = ['src/'+s for s in _sources]
    bld.objects(source=sources,
                cxxflags=cxxflags,
                target='common_objects',
                includes='.',
                use="BOOST")

    if not _disable_tests:
        _tests(bld)

    bld(
        features='subst',
        source='scripts/version.template',
        target='version.c',
        REVISION=_conf_get_git_rev())

    bld.program(
        source=['src/koi.cpp', 'src/run.cpp', 'version.c'],
        target='koinode',
        lib=['pthread'],
        cxxflags=cxxflags,
        linkflags=ldflags,
        use='common_objects BOOST',
        install_path='${PREFIX}/sbin',
        includes='.')

    bld.program(
        source=['src/cli.cpp', 'version.c'],
        target='koi',
        lib=['pthread'],
        cxxflags=cxxflags,
        linkflags=ldflags,
        use='common_objects BOOST',
        install_path='${PREFIX}/sbin',
        includes='.')

    if not _disable_tests:
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
