#include "MediaControl.h"

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
		Sleep(1);
	}
	control->Close();
	delete control;
	return 0;
}