"""RetyOs: Windows build tools for Android on Kindle."""
import argparse
from tools.build_system import bootstrap,prepare,compile_all,package,doctor

def main():
    p=argparse.ArgumentParser(description=__doc__)
    sub=p.add_subparsers(dest='command',required=True)
    b=sub.add_parser('bootstrap',help='Fetch/verify pinned external tools; never accesses Kindle')
    b.add_argument('--cache',help='Optional directory containing already downloaded inputs')
    b.add_argument('--java8-home',required=True,help='Installed JDK8 home; copied as a build dependency')
    for name in ('doctor','prepare','build','package','verify','all'):
        s=sub.add_parser(name)
        if name in ('build','all'):s.add_argument('--resume',action='store_true',help='Resume from the last successful build step')
    a=p.parse_args()
    if a.command=='bootstrap':bootstrap(a.cache,a.java8_home)
    elif a.command=='doctor':doctor()
    elif a.command=='prepare':prepare()
    elif a.command=='build':compile_all(a.resume)
    elif a.command=='package':package()
    elif a.command=='verify':
        from tools.verify_package import verify
        verify()
    else:
        prepare();compile_all(a.resume);package()
        from tools.verify_package import verify
        verify()

if __name__=='__main__':main()
