"""Read-only, standard-library checks for a PYNQ source submission."""
from pathlib import Path
import argparse,ast,json,re
from urllib.parse import unquote

def check(root):
    root=root.resolve();errors=[];count=0
    for name in ['src','sim','build','board','data','skill','report']:
        if not (root/name).is_dir():errors.append('missing directory: '+name)
    if not (root/'README.md').is_file():errors.append('missing README.md')
    for p in root.rglob('*'):
        rel=p.relative_to(root)
        if any(s in {'.git','.venv','node_modules','__pycache__','.pytest_cache'} for s in rel.parts):continue
        if any(not re.fullmatch(r'[a-z0-9_.-]+',s) and s!='README.md' for s in rel.parts):errors.append('invalid name: '+rel.as_posix())
        if not p.is_file():continue
        count+=1
        if p.suffix=='.py':
            try:ast.parse(p.read_text(encoding='utf-8-sig'),filename=str(p))
            except (SyntaxError,UnicodeError) as e:errors.append('python syntax: '+rel.as_posix()+': '+str(e))
        if p.suffix=='.md':
            text=p.read_text(encoding='utf-8-sig')
            # Fenced shell/code samples do not constitute Markdown links.
            text=re.sub(r'```.*?```','',text,flags=re.S)
            for dest in re.findall(r'\]\(([^)\s]+)\)',text):
                if '://' in dest or dest.startswith(('#','mailto:')):continue
                target=unquote(dest.split('#')[0])
                if target and not (p.parent/target).exists():errors.append('broken local link: '+rel.as_posix()+' -> '+dest)
    artifacts={ext:sorted(p.relative_to(root).as_posix() for p in root.rglob('*.'+ext) if '.git' not in p.parts) for ext in ['bit','xsa','elf']}
    return {'passed':not errors,'files_checked':count,'errors':errors,'artifacts':artifacts,'hardware_ready':all(artifacts.values()),'scope':'Names, directories, local Markdown links, Python syntax only; no hardware commands executed.'}

if __name__=='__main__':
    ap=argparse.ArgumentParser();ap.add_argument('root',nargs='?',default=str(Path(__file__).resolve().parents[1]));args=ap.parse_args()
    result=check(Path(args.root));print(json.dumps(result,ensure_ascii=False,indent=2));raise SystemExit(0 if result['passed'] else 1)
