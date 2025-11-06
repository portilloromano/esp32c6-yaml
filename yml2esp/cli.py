
import argparse, sys, os, yaml, json, subprocess
from jsonschema import validate, Draft202012Validator
from jinja2 import Environment, FileSystemLoader

def load_yaml(p):
    with open(p, "r") as f:
        return yaml.safe_load(f)

def load_schema(root):
    with open(os.path.join(root, "schema.json"), "r") as f:
        return json.load(f)

def validate_yaml(data, schema):
    v = Draft202012Validator(schema)
    errs = sorted(v.iter_errors(data), key=lambda e: e.path)
    if errs:
        msg = "\n".join([str(e.message) for e in errs])
        raise SystemExit(msg)

def render(env, data, outdir):
    os.makedirs(outdir, exist_ok=True)
    src = os.path.join(outdir, "main")
    os.makedirs(src, exist_ok=True)
    for t in ["driver_led_strip.c.j2","button.c.j2","matter_glue.cpp.j2","main.cpp.j2","CMakeLists.txt.j2","sdkconfig.j2"]:
        tpl = env.get_template(t)
        outname = t.replace(".j2","")
        with open(os.path.join(src, outname), "w") as f:
            f.write(tpl.render(app=data.get("app",{}), fabrication=data.get("fabrication",{})))
    return src

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
