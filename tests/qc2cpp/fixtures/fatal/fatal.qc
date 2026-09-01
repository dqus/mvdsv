// Minimal real-server fixture for the terminal-ordering acceptance test.
// worldspawn creates an actor and an overlapping trigger.  The observer runs
// the actor's think callback, which crosses walkmove -> SV_TouchLinks -> touch.

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
entity trigger_entity;
entity actor_entity;

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

entity() spawn = #14;
void(entity e, vector origin) setorigin = #2;
void(entity e, vector mins, vector maxs) setsize = #4;
void(string message) dprint = #25;
void(string message) error = #10;
string(string model) precache_model = #20;
float(float yaw, float distance) walkmove = #32;

void() fatal_touch =
{
    dprint("[qc2cpp-fatal] nested-touch\n");
    error("controlled nested qc2cpp fatal");
};

void() fatal_outer =
{
    dprint("[qc2cpp-fatal] outer-gameplay-entered\n");
    walkmove(0, 0);
    dprint("[qc2cpp-fatal] outer-gameplay-resumed\n");
};

void() worldspawn =
{
	precache_model("progs/player.mdl");
    trigger_entity = spawn();
    trigger_entity.solid = 1;
    trigger_entity.touch = fatal_touch;
    setsize(trigger_entity, '-16 -16 -16', '16 16 16');
    setorigin(trigger_entity, '0 0 0');

    actor_entity = spawn();
    actor_entity.flags = 512;
    actor_entity.solid = 2;
    actor_entity.think = fatal_outer;
    setsize(actor_entity, '-8 -8 -8', '8 8 8');
    setorigin(actor_entity, '0 0 0');
};
