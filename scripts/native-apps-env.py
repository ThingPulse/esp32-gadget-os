"""Use an app-only builder instead of the firmware builder for deployment targets."""
from pathlib import Path
Import('env')
env.Replace(BUILD_SCRIPT=str(Path(env.subst('$PROJECT_DIR')) / 'scripts/native-apps-target.py'))
