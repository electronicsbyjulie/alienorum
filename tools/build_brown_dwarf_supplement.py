import csv,json,re,math,statistics,argparse
parser=argparse.ArgumentParser()
parser.add_argument("input")
args=parser.parse_args()
from pathlib import Path
rows=list(csv.DictReader(open(args.input,newline='')))
byid={r['SystemHierarchy']:r for r in rows}
def number(s):
 try:
  v=float(s.rstrip('?'))
  return v if math.isfinite(v) else None
 except ValueError:return None
def inherited(r,k):
 while not r[k] and '.' in r['SystemHierarchy']:
  r=byid.get(r['SystemHierarchy'].rsplit('.',1)[0],r)
  if not '.' in r['SystemHierarchy']: break
 return r[k]
# Legacy component labels used by the condensed catalog.
legacy_components = {"GJ845B": "Eps Ind B", "GJ845C": "Eps Ind C"}
records=[]
for r in rows:
 key=r['SystemHierarchy']
 if any(k.startswith(key+'.') for k in byid):continue
 spec=r['SpecTypeNIR'] or r['SpecTypeOpt']
 mass=number(r['Mass (Msun)']) or number(r['AdoptedInitialMass (Msun)'])
 if not (re.match(r'(?:[eu]?sd)?[LTY]',spec) or (mass and 0<mass<.075)):continue
 aliases=[]
 for k in ['DefaultName','CommonName','Bayer/Flamsteed','GJ','2MASS','WISE','Gaia','HD','HIP','LHS','LP','Wolf']:
  v=r[k].lstrip('-').strip()
  if not v:continue
  if k in ['HD','HIP','LHS','LP','Wolf'] and not v.startswith(k):v=k+' '+v
  if v not in aliases:aliases.append(v)
 legacy=legacy_components.get(''.join(r['GJ'].upper().split()))
 if legacy and legacy not in aliases:aliases.append(legacy)
 out={'name':r['DefaultName'].lstrip('-').strip(),'aliases':aliases,'spectrum':spec,'hierarchy':key,'system':r['SystemCode']}
 for dest,src in [('ra','RA (deg)'),('dec','Dec (deg)'),('epoch','Epoch'),('parallax','Parallax (mas)'),('pmra','PMRA (mas/yr)'),('pmdec','PMDec (mas/yr)')]:
  v=number(inherited(r,src))
  if v is not None:out[dest]=v
 for dest,src,factor in [('ra','SexagesimalRA',15),('dec','SexagesimalDec',1)]:
  if dest not in out:
   value=inherited(r,src)
   parts=re.findall(r'[+-]?\d+(?:\.\d+)?',value)
   if len(parts)>=3:
    sign=-1 if parts[0].startswith('-') else 1
    out[dest]=sign*(abs(float(parts[0]))+float(parts[1])/60+float(parts[2])/3600)*factor
 if 'ra' not in out or 'dec' not in out:
  members=[x for x in rows if x['SystemCode']==r['SystemCode'] and x['RA (deg)'] and x['Dec (deg)']]
  if members:
   out['ra']=float(members[0]['RA (deg)']);out['dec']=float(members[0]['Dec (deg)'])
   out['position_from_system_member']=True
 for dest,src in [('temperature','Teff (K)'),('gmag','G (mag)'),('radial_velocity','Gaia_RV')]:
  v=number(r[src])
  if v is not None:out[dest]=v
 if mass and mass>0:out['mass_solar']=mass
 out['notes']=r['GeneralNotes'];out['astrometry_reference']=inherited(r,'PlxPMRef');out['spectrum_reference']=r['SpTNIR_ref'] or r['SpTOpt_ref']
 records.append(out)
def spectral_index(text):
 m=re.search(r'([LTY])(\d+(?:\.\d+)?)',text)
 return {'L':0,'T':10,'Y':20}[m[1]]+float(m[2]) if m else None
for out in records:
 if out.get('parallax',0)>0:continue
 source=byid[out['hierarchy']]
 target=spectral_index(out['spectrum'])
 if target is None:continue
 for band in ['W2 (mag)','J2MASS (mag)','JMKO (mag)']:
  apparent=number(source[band])
  if apparent is None:continue
  samples=[]
  for other in rows:
   index=spectral_index(other['SpecTypeNIR'] or other['SpecTypeOpt'])
   parallax=number(other['Parallax (mas)']);mag=number(other[band])
   if index is not None and abs(index-target)<=2 and parallax and parallax>0 and mag is not None:
    samples.append(mag-5*math.log10(1000/parallax)+5)
  if len(samples)>=3:
   out['distance_pc']=10**((apparent-statistics.median(samples)+5)/5)
   out['distance_estimated']=True
   out['distance_method']=band+' median absolute magnitude of nearby spectral subtypes in this census'
   break
Path('catalogs/brown_dwarfs_20pc.json').write_text(json.dumps({'source':'Kirkpatrick et al. 2024, ApJS 271, 55; NASA Exoplanet Archive 20pc Census Table 4','doi':'10.3847/1538-4365/ad24e2','url':'https://exoplanetarchive.ipac.caltech.edu/docs/20pcCensus.html','records':records},indent=2)+'\n')
print(len(records),'individual substellar records',sum(all(k in r for k in ['ra','dec','parallax']) for r in records),'with positions')
