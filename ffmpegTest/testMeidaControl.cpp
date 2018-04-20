#include "MediaControl.h"
#ifdef _WIN32
#define sleep Sleep
#else
#include <unistd.h>
#endif

int testMediaControl(const char * file)
{
	MediaControl * control = new MediaControl();
	int ret = control->Open(file);
	if (ret != 0) return -1;
	ret = control->Play();
	if (ret != 0) return -1;
	while (true)
	{
		if (control->CheckIsEnd()) break;
		sleep(1);
	}
	control->Close();
	delete control;
	return 0;
}