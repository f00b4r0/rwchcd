#include "../hw_backends/hw_p1/hw_p1.c"

int main(void)
{
	int i;
	time_t rtime;
	int y, d, h, m, s;
	struct s_hw_p1_pdata Hardware;

	memset(&Hardware, 0x0, sizeof(Hardware));

	storage_online();
	hw_p1_restore_relays(&Hardware);

	for (i=0; i<ARRAY_SIZE(Hardware.Relays); i++) {
		if (!Hardware.Relays[i].run.cycles)
			continue;
		printf("Relay: %d\n", i+1);
		rtime = (Hardware.Relays[i].run.on_totsecs);
		y = rtime / (86400 * 365);
		d = (rtime / 86400) % 365;
		h = (rtime / 3600) % 24;
		m = (rtime / 60) % 60;
		s = rtime % 60;
		printf("\tTotal on time: %d:%d:%02d:%02d:%02d\n", y, d, h, m, s);
		rtime = (Hardware.Relays[i].run.off_totsecs);
		y = rtime / (86400 * 365);
		d = (rtime / 86400) % 365;
		h = (rtime / 3600) % 24;
		m = (rtime / 60) % 60;
		s = rtime % 60;
		printf("\tTotal off time: %d:%d:%02d:%02d:%02d\n", y, d, h, m, s);
		printf("\tTotal cycles: %d\n", Hardware.Relays[i].run.cycles);
		printf("\n");
	}

	return (0);
}
