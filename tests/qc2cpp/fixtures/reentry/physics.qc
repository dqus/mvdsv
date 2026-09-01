float time;
float parm1;
float parm2;
float parm3;

void() reentry_touch_one =
{
	parm1 = time;
	time = 17;
};

void() reentry_touch_two =
{
	parm2 = time;
};

void() reentry_think =
{
	parm3 = time;
};
