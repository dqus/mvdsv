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

float(string name, string value) registercvar = #93;
float(string name) cvar = #45;
void(string name, string value) cvar_set = #72;
void(string message) dprint = #25;
void(string message) error = #10;
string(string name) precache_model = #20;

float first_registration;
float existing_registration;
float seen_request;
float restore_marker;

void() worldspawn = {
    precache_model("progs/player.mdl");
    first_registration = registercvar("qcx_reg_probe", "7.25");
    existing_registration = registercvar("qcx_reg_existing", "99");
};

void() StartFrame = {
    local float request = cvar("qcx_reg_request");
    if (request <= 0 || request == seen_request)
        return;
    seen_request = request;
    if (request == 3)
        restore_marker = 17;
    if (request == 4)
        restore_marker = 23;
    if (request == 5 && restore_marker != 17)
        error("game restore did not restore its saved global");
    if (cvar("qcx_reg_action") == 1)
        cvar_set("qcx_reg_probe", "17");
    if (cvar("qcx_reg_action") == 2)
        cvar_set("qcx_reg_probe", "23");
    if (registercvar("qcx_reg_probe", "999") != 0 || existing_registration != 0
        || cvar("qcx_reg_existing") != 41)
        error("registercvar overwrote an existing cvar or returned the wrong float");
    if ((request < 3 && first_registration != 1) || (request >= 3 && first_registration != 0))
        error("registercvar returned the wrong creation float");
    if ((request == 1 && cvar("qcx_reg_probe") != 7.25)
        || ((request == 2 || request == 3) && cvar("qcx_reg_probe") != 17)
        || (request >= 4 && cvar("qcx_reg_probe") != 23))
        error("registercvar/map/save-load changed the engine value");
    // One complete line per import: dprint itself appends a server newline.
    if (request == 1) dprint("{\"registercvar_probe\":{\"request\":1,\"pass\":1}}");
    if (request == 2) dprint("{\"registercvar_probe\":{\"request\":2,\"pass\":1}}");
    if (request == 3) dprint("{\"registercvar_probe\":{\"request\":3,\"pass\":1}}");
    if (request == 4) dprint("{\"registercvar_probe\":{\"request\":4,\"pass\":1}}");
    if (request == 5) dprint("{\"registercvar_probe\":{\"request\":5,\"pass\":1}}");
};
