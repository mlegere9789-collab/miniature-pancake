import sys
s=open(sys.argv[1]).read(); depth=0; i=0; line=1; instr=False; mind=0
while i<len(s):
    c=s[i]
    if c=='\n': line+=1
    if instr:
        if c=='\\': i+=1
        elif c=='"': instr=False
    else:
        if c=='"': instr=True
        elif c==';':
            if s[i+1:i+2]=='|':
                j=s.index('|;',i); line+=s[i:j].count('\n'); i=j+1
            else:
                while i<len(s) and s[i]!='\n': i+=1
                line+=1
        elif c=='(': depth+=1
        elif c==')':
            depth-=1
            if depth<0: print('negative depth at line',line); sys.exit(1)
    i+=1
print('final depth',depth,'in string',instr)
