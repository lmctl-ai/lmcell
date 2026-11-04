"""Preserve whole gallery workflows, including separate-process parameter sweeps."""
import hashlib, re, sys, tempfile
from pathlib import Path
from invocation import run_sources
root=Path.cwd()
work=Path(sys.argv[1])
sources=sorted((root/'examples').glob('*.lms'))
assert len(sources)==16, len(sources)
for source in sources:
    text=source.read_text()
    assert 'r.stdout' not in text, source
    renderer=re.search(r'^\s*\w+\s*=\s*_lmcode_ cli="(?:lmplot|lmcad|lmdraw|lmblender)"',text,re.M)
    assert renderer, source
    prefix=text[:renderer.start()].replace(' install="github:lmctl-ai/lmcell@latest"','')
    driver=work/(source.stem+'-offline.lms')
    driver.write_text(prefix)
    capture=work/(source.stem+'.capture')
    result=run_sources([driver],example=True,capture=capture,timeout=1800)
    assert result.returncode==0,(source.name,result.stdout,result.stderr)
    data=(capture/'0.lmcode').read_bytes()
    assert data[:6]==b'DSLB\0\6'
    assert data[6:38]==hashlib.sha256((root/'schema/lmcell.dsl.json').read_bytes()).digest()
print('PASS 16 complete offline gallery workflows, including parameter sweeps')
