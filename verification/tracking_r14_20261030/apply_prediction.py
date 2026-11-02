from pathlib import Path
H=Path(__file__).parent;P=H.parents[1];f=P/'firmware/src/obstacle_motion_core.c';s=f.read_text();a=s.index('        unsigned xa=umax(t->bbox.x,c->bbox.x)');b=s.index('        unsigned appearance=',a)
s=s[:a]+'''        /* Distance and overlap must describe the same capture timestamp. */
        int px=(int)t->bbox.x+dx,py=(int)t->bbox.y+dy;
        int xa=px>(int)c->bbox.x?px:(int)c->bbox.x;
        int ya=py>(int)c->bbox.y?py:(int)c->bbox.y;
        int xb=px+t->bbox.w<(int)c->bbox.x+c->bbox.w?px+t->bbox.w:(int)c->bbox.x+c->bbox.w;
        int yb=py+t->bbox.h<(int)c->bbox.y+c->bbox.h?py+t->bbox.h:(int)c->bbox.y+c->bbox.h;
        unsigned inter=xb>xa&&yb>ya?(unsigned)((xb-xa)*(yb-ya)):0u;
'''+s[b:];f.write_text(s)
