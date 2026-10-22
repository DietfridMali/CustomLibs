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
	if (viewCount < 2)
		return folder;
	return String(FilePath(folder, String("vr")).string().c_str());
}

}
