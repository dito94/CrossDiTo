"""
PlatformIO pre-build script: inject git info into version defines.

  x4-pro:          1.1.0-dev+<branch>         (local development build)
  x4-pro-debug:    1.1.0-debug-<branch>+<hash>
  production:      1.1.0                      (when $CROSSINK_RELEASE_VERSION is set)
  release candidate: 1.1.0-rc+<hash>

The X4 Pro simulator sets CROSSINK_VERSION directly in platformio.ini. The
legacy macro name is retained for source and settings compatibility.
"""

import configparser
import os
import re
import subprocess
import sys


def warn(msg):
    print(f'WARNING [git_branch.py]: {msg}', file=sys.stderr)


def get_git_short_hash(project_dir, length=5):
    try:
        return subprocess.check_output(
            ['git', 'rev-parse', '--short', 'HEAD'],
            text=True, stderr=subprocess.PIPE, cwd=project_dir
        ).strip()[:length]
    except Exception as e:
        warn(f'Could not read git hash: {e}; hash will be "00000"')
        return '00000'


def run_git_value(project_dir, args, label):
    try:
        value = subprocess.check_output(
            ['git', *args],
            text=True, stderr=subprocess.PIPE, cwd=project_dir
        ).strip()
        # Strip characters that would break a C string literal
        return ''.join(c for c in value if c not in '"\\')
    except FileNotFoundError:
        warn(f'git not found on PATH; {label} suffix will be "unknown"')
        return 'unknown'
    except subprocess.CalledProcessError as e:
        warn(
            f'git command failed (exit {e.returncode}): '
            f'{e.stderr.strip()}; {label} suffix will be "unknown"'
        )
        return 'unknown'
    except OSError as e:
        warn(
            f'OS error reading git {label}: {e}; '
            f'{label} suffix will be "unknown"'
        )
        return 'unknown'
    except Exception as e:  # pylint: disable=broad-exception-caught
        warn(
            f'Unexpected error reading git {label}: {e}; '
            f'{label} suffix will be "unknown"'
        )
        return 'unknown'


def get_git_branch(project_dir):
    branch = run_git_value(
        project_dir, ['rev-parse', '--abbrev-ref', 'HEAD'], 'branch'
    )
    # Detached HEAD has no branch name.
    if branch == 'HEAD':
        return 'detached'
    return sanitize_version_component(branch)


def sanitize_version_component(value):
    value = value.strip()
    value = re.sub(r'[^A-Za-z0-9._-]+', '-', value)
    value = re.sub(r'-{2,}', '-', value)
    value = value.strip('-.')
    return value or 'unknown'


def _read_ini(project_dir):
    ini_path = os.path.join(project_dir, 'platformio.ini')
    local_ini_path = os.path.join(project_dir, 'platformio.local.ini')
    config = configparser.ConfigParser()
    if os.path.isfile(ini_path):
        config_paths = [ini_path]
        if os.path.isfile(local_ini_path):
            # Match PlatformIO's local override convention: values from the
            # optional local file take precedence over the tracked config.
            config_paths.append(local_ini_path)
        config.read(config_paths)
    else:
        warn(f'platformio.ini not found at {ini_path}')
    return config


def get_crossdito_version(project_dir):
    config = _read_ini(project_dir)
    if not config.has_option('crossdito', 'version'):
        warn(
            'No [crossdito] version in platformio.ini or platformio.local.ini; '
            'build version will be "0.0.0"'
        )
        return '0.0.0'
    return config.get('crossdito', 'version')


def get_production_version(project_dir):
    release_version = os.environ.get('CROSSINK_RELEASE_VERSION')
    if release_version:
        return sanitize_version_component(release_version.lstrip('v'))
    return get_crossdito_version(project_dir)


def get_firmware_version(project_dir, pioenv):
    if os.environ.get('CROSSINK_RC_HASH'):
        short_hash = sanitize_version_component(os.environ['CROSSINK_RC_HASH'])
        return f'{get_crossdito_version(project_dir)}-rc+{short_hash}'
    if os.environ.get('CROSSINK_RELEASE_VERSION'):
        return get_production_version(project_dir)
    version = get_crossdito_version(project_dir)
    branch = get_git_branch(project_dir)
    # Exported source snapshots intentionally have no .git directory. They are
    # still reproducible release sources, so show the configured version rather
    # than presenting the user with a misleading "unknown" build identity.
    if branch == 'unknown':
        return version
    return f'{version}-dev+{branch}'


def inject_version(env):
    project_dir = env['PROJECT_DIR']
    pioenv = env['PIOENV']

    if pioenv == 'x4-pro':
        version_string = get_firmware_version(project_dir, pioenv)
        if os.environ.get('CROSSINK_RC_HASH'):
            print(f'CrossDiTo RC build version: {version_string}')
        elif os.environ.get('CROSSINK_RELEASE_VERSION'):
            print(f'CrossDiTo production build version: {version_string}')
        else:
            print(f'CrossDiTo build version: {version_string}')
        env.Append(CPPDEFINES=[('CROSSINK_VERSION', f'\\"{version_string}\\"')])

    elif pioenv == 'x4-pro-debug':
        branch = get_git_branch(project_dir)
        short_hash = get_git_short_hash(project_dir)
        ci_version = get_crossdito_version(project_dir)
        suffix = f'-debug-{branch}+{short_hash}'
        env.Append(CPPDEFINES=[
            ('CROSSINK_VERSION', f'\\"{ci_version}{suffix}\\"'),
            ('CROSSINK_BUILD_ENV', '\\"debug\\"'),
            'CROSSINK_SHOW_SLEEP_BUILD_INFO',
        ])
        print(f'CrossDiTo debug build version: {ci_version}{suffix}')


# PlatformIO/SCons entry point — Import and env are SCons builtins injected at runtime.
# When run directly with Python (e.g. for validation), a lightweight fake env is used
# so the git/version logic can be exercised without a full build.
try:
    Import('env')  # noqa: F821  # type: ignore[name-defined]
except NameError:
    class _Env(dict):
        def Append(self, **_): pass

    if '__file__' in globals():
        _project_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    else:
        _project_dir = os.getcwd()
    inject_version(_Env({'PIOENV': 'x4-pro', 'PROJECT_DIR': _project_dir}))
else:
    inject_version(env)  # noqa: F821  # type: ignore[name-defined]
