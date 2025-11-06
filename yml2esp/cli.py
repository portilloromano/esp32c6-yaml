
import argparse, os, yaml, json, subprocess
from jsonschema import validate, Draft202012Validator
from jinja2 import Environment, FileSystemLoader

def load_yaml(p):
    with open(p, "r") as f:
        return yaml.safe_load(f)

def load_schema(root):
    with open(os.path.join(root, "schema.json"), "r") as f:
        return json.load(f)

def load_manifest(root):
    return load_yaml(os.path.join(root, "templates", "manifest.yaml"))

def get_nested_value(data, key):
    current = data
    for part in key.split("."):
        if isinstance(current, dict) and part in current:
            current = current[part]
        else:
            return None
    return current

def matches_when(when, data):
    if not when:
        return True
    if not isinstance(when, dict):
        return False
    for key, expected in when.items():
        if get_nested_value(data, key) != expected:
            return False
    return True

def validate_yaml(data, schema):
    v = Draft202012Validator(schema)
    errs = sorted(v.iter_errors(data), key=lambda e: e.path)
    if errs:
        msg = "\n".join([str(e.message) for e in errs])
        raise SystemExit(msg)

def render(env, data, outdir):
    os.makedirs(outdir, exist_ok=True)
    manifest = load_manifest(os.path.dirname(__file__))
    if not isinstance(manifest, dict):
        manifest = {}
    templates = []
    if not isinstance(data, dict):
        data = {}
    for block in manifest.values():
        if not isinstance(block, dict):
            continue
        if not matches_when(block.get("when"), data):
            continue
        for entry in block.get("templates", []):
            if not isinstance(entry, dict):
                continue
            if not matches_when(entry.get("when"), data):
                continue
            templates.append((entry["template"], entry["output"]))
    for template_name, output_path in templates:
        tpl = env.get_template(template_name)
        rendered = tpl.render(**data)
        destination = os.path.join(outdir, output_path)
        dest_dir = os.path.dirname(destination)
        if dest_dir:
            os.makedirs(dest_dir, exist_ok=True)
        with open(destination, "w") as f:
            f.write(rendered)
    main_dir = os.path.join(outdir, "main")
    return main_dir if os.path.isdir(main_dir) else outdir

def run(cmd, cwd=None):
    p = subprocess.Popen(cmd, cwd=cwd)
    p.communicate()
    if p.returncode != 0:
        raise SystemExit(p.returncode)

def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest="cmd", required=True)
    p_validate = sub.add_parser("validate")
    p_validate.add_argument("yaml")
    p_render = sub.add_parser("render")
    p_render.add_argument("yaml")
    p_render.add_argument("--out", required=True)
    p_build = sub.add_parser("build")
    p_build.add_argument("--project", required=True)
    p_flash = sub.add_parser("flash")
    p_flash.add_argument("--project", required=True)
    p_flash.add_argument("--port", required=True)
    args = parser.parse_args()
    root = os.path.dirname(__file__)
    if args.cmd == "validate":
        data = load_yaml(args.yaml)
        schema = load_schema(root)
        validate_yaml(data, schema)
        print("ok")
    if args.cmd == "render":
        data = load_yaml(args.yaml)
        schema = load_schema(root)
        validate_yaml(data, schema)
        env = Environment(loader=FileSystemLoader(os.path.join(root,"templates")), trim_blocks=True, lstrip_blocks=True)
        outdir = args.out
        src = render(env, data, outdir)
        print(src)
    if args.cmd == "build":
        run(["idf.py","reconfigure"], cwd=args.project)
        run(["idf.py","build"], cwd=args.project)
    if args.cmd == "flash":
        run(["idf.py","-p",args.port,"flash","monitor"], cwd=args.project)

if __name__ == "__main__":
    main()
