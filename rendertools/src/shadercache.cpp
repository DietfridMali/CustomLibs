#include "shadercache.h"
#include "gfxrenderer.h"

namespace ShaderCache {

static String& Folder(void)
{
	static String folder;
	return folder;
}


void SetFolder(const String& folder)
{
	Folder() = folder;
}


String GetFolder(int viewCount)
{
	const String& folder = Folder();

	if (folder.IsEmpty())
		return String();
	if (viewCount < 1)
		viewCount = baseRenderer.ViewCount();
	return String(FilePath(folder, String((viewCount > 1) ? "stereo" : "mono")).string().c_str());
}

}
