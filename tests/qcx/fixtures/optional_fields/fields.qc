entity self;
entity other;
entity world;
float time;
float frametime;
float deathmatch;
float coop;
float teamplay;
string mapname;
float serverflags;
float parm1; float parm2; float parm3; float parm4;
float parm5; float parm6; float parm7; float parm8;
float parm9; float parm10; float parm11; float parm12;
float parm13; float parm14; float parm15; float parm16;

.float flags;
.float solid;
.vector origin;
.vector mins;
.vector maxs;
.vector size;
.void() touch;
.void() think;
.float nextthink;
.string classname;
.string model;
.string weaponmodel;
.string netname;
.string target;
.string targetname;
.string message;
.string noise;
.string noise1;
.string noise2;
.string noise3;

.float items2;
.float maxspeed;
.float gravity;
.vector movement;
.float vw_index;
.entity hideentity;
.entity trackent;
.float visclients;
.float hideplayers;
.float teleported;

string(string model) precache_model = #20;

void() worldspawn =
{
    precache_model("progs/player.mdl");
};
