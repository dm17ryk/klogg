"""Validate the planning package only; does not test the CILogg product."""
from pathlib import Path
import collections,json,re,sys

root=Path(sys.argv[1] if len(sys.argv)>1 else '.').resolve()
issues=[]
index=json.loads((root/'TASK_INDEX.json').read_text(encoding='utf-8'))
tasks=index['tasks']; ids={t['id'] for t in tasks}
if len(ids)!=len(tasks):issues.append('Duplicate task IDs')
for t in tasks:
 for d in t['dependencies']:
  if d not in ids:issues.append(f"Unknown dependency {t['id']} -> {d}")
 if t['id'] in t['dependencies']:issues.append('Self dependency '+t['id'])
 for field in ['plan','taskFile']:
  if not (root/t[field]).is_file():issues.append('Missing '+t[field])
 text=(root/t['taskFile']).read_text(encoding='utf-8')
 if len(re.findall(r'^\s*- \[ \]',text,re.M))<3:issues.append('Too few checkable steps '+t['id'])
 if 'Integrated hard dependencies' not in text:issues.append('No resolved dependency list '+t['id'])
done=set(); order=[]
while len(done)<len(ids):
 ready=sorted(t['id'] for t in tasks if t['id'] not in done and set(t['dependencies'])<=done)
 if not ready:
  issues.append('Cyclic/unresolved graph: '+','.join(sorted(ids-done)));break
 done.update(ready);order+=ready
requirements=json.loads((root/'FEATURE_COVERAGE.json').read_text(encoding='utf-8'))
if len({r['id'] for r in requirements})!=len(requirements):issues.append('Duplicate requirements')
for r in requirements:
 if not r['tasks']:issues.append('Unowned requirement '+r['id'])
 for t in r['tasks']:
  if t not in ids:issues.append('Unknown requirement owner '+r['id']+' -> '+t)
progress=json.loads((root/'PROGRESS.json').read_text(encoding='utf-8'))
if set(progress['tasks'])!=ids:issues.append('Progress ledger misses tasks')
links=0; docs=0
for path in root.rglob('*.md'):
 docs+=1;text=path.read_text(encoding='utf-8')
 fence=None
 for line in text.splitlines():
  m=re.match(r'^\s*(`{3,}|~{3,})',line)
  if m:
   mark=m.group(1)[0]
   if fence is None:fence=mark
   elif fence==mark:fence=None
 if fence:issues.append('Unbalanced code fence '+str(path.relative_to(root)))
 if re.search(r'\b(?:TBD|TODO)\b',text):issues.append('Unresolved planning placeholder '+str(path.relative_to(root)))
 for href in re.findall(r'(?<!!)\[[^\]]*\]\(([^)]+)\)',text):
  if href.startswith(('https://','http://','mailto:','#')):continue
  target=href.split('#')[0]
  if not target:continue
  links+=1
  if not (path.parent/target).exists():issues.append(f'Broken local link {path.relative_to(root)} -> {href}')
 # Named reference links must have definitions in the same standalone card/file.
 definitions={m.lower() for m in re.findall(r'^\[([^\]]+)\]:',text,re.M)}
 for label in re.findall(r'\[([BDR][1-9]\d*)\](?![(:])',text):
  if label in ids:continue
  if label.lower() not in definitions:issues.append(f'Undefined source reference {path.relative_to(root)} [{label}]')

result={'valid':not issues,'taskCount':len(tasks),'requirementCount':len(requirements),'markdownFiles':docs,'validatedLocalLinks':links,'topologicalTaskCount':len(done),'issues':issues}
print(json.dumps(result,indent=2))
sys.exit(0 if not issues else 1)
