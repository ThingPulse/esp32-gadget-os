"""Embed the installable app catalog in the offline desktop simulator."""
import json
from pathlib import Path
Import("env")
root = Path(env.subst("$PROJECT_DIR"))
apps = json.loads((root / "experiments/native-app/apps.json").read_text())["apps"]
names = {"transit":"Transit","planes":"Planes","home_assistant":"Home Assistant","stocks": "Stocks", "agent_usage": "Agent Dashboard", "system_monitor": "System Monitor", "iss_tracker": "ISS"}
entries = []
for app in apps:
    entries.append("{" + ",".join(json.dumps(value) for value in
        (app["id"], names[app["source"]], app["version"], app["description"], "")) + ",0}")
generated = Path(env.subst("$BUILD_DIR")) / "generated"
generated.mkdir(parents=True, exist_ok=True)
header = generated / "SimulatorCatalog.inc"
content = ",\n".join(entries) + "\n"
if not header.exists() or header.read_text() != content:
    header.write_text(content)
env.Append(CPPPATH=[str(generated.resolve())])
