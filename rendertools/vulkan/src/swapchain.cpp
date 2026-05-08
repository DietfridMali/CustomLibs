#include "swapchain.h"
#include "vkcontext.h"

#include <cstdio>
#include <algorithm>

// =================================================================================================
// Swapchain

bool Swapchain::PickSurfaceFormat(VkSurfaceKHR surface) noexcept
{
    VkPhysicalDevice phys = vkContext.PhysicalDevice();

    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &count, nullptr);
    if (count == 0)
        return false;

    constexpr uint32_t kMax = 64;
    if (count > kMax)
        count = kMax;
    VkSurfaceFormatKHR formats[kMax] { };
    vkGetPhysicalDeviceSurfaceFormatsKHR(phys, surface, &count, formats);

    // Preferred: B8G8R8A8_UNORM + sRGB nonlinear. Fall back to first.
    for (uint32_t i = 0; i < count; ++i) {
        if ((formats[i].format == VK_FORMAT_B8G8R8A8_UNORM)
            and (formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)) {
            m_format = formats[i].format;
            m_colorSpace = formats[i].colorSpace;
            return true;
        }
    }
    m_format = formats[0].format;
    m_colorSpace = formats[0].colorSpace;
    return true;
}


bool Swapchain::PickPresentMode(VkSurfaceKHR surface, bool vsync) noexcept
{
    VkPhysicalDevice phys = vkContext.PhysicalDevice();

    uint32_t count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &count, nullptr);
    if (count == 0)
        return false;

    constexpr uint32_t kMax = 8;
    if (count > kMax)
        count = kMax;
    VkPresentModeKHR modes[kMax] { };
    vkGetPhysicalDeviceSurfacePresentModesKHR(phys, surface, &count, modes);

    if (vsync) {
        // FIFO is guaranteed to be available; matches DX12 syncInterval=1.
        m_presentMode = VK_PRESENT_MODE_FIFO_KHR;
        return true;
    }

    // No vsync — prefer MAILBOX (low-latency triple buffer), fall back to IMMEDIATE, then FIFO.
    bool hasMailbox = false;
    bool hasImmediate = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR) hasMailbox = true;
        if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) hasImmediate = true;
    }
    m_presentMode = hasMailbox ? VK_PRESENT_MODE_MAILBOX_KHR
                  : hasImmediate ? VK_PRESENT_MODE_IMMEDIATE_KHR
                  : VK_PRESENT_MODE_FIFO_KHR;
    return true;
}


bool Swapchain::AcquireImages(void) noexcept
{
    VkDevice device = vkContext.Device();

    uint32_t count = 0;
    vkGetSwapchainImagesKHR(device, m_swapchain, &count, nullptr);
    if (count == 0) {
        fprintf(stderr, "Swapchain::AcquireImages: vkGetSwapchainImagesKHR returned 0\n");
        return false;
    }
    if (count > MAX_BACK_BUFFERS) {
        fprintf(stderr, "Swapchain::AcquireImages: %u images > MAX_BACK_BUFFERS %u — truncating\n",
                count, MAX_BACK_BUFFERS);
        count = MAX_BACK_BUFFERS;
    }
    m_imageCount = count;
    vkGetSwapchainImagesKHR(device, m_swapchain, &count, m_images);

    for (uint32_t i = 0; i < count; ++i) {
        VkImageViewCreateInfo info { };
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = m_images[i];
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = m_format;
        info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        info.subresourceRange.baseMipLevel = 0;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.baseArrayLayer = 0;
        info.subresourceRange.layerCount = 1;

        VkResult res = vkCreateImageView(device, &info, nullptr, &m_imageViews[i]);
        if (res != VK_SUCCESS) {
            fprintf(stderr, "Swapchain::AcquireImages: vkCreateImageView[%u] failed (%d)\n", i, (int)res);
            return false;
        }
        m_layoutTrackers[i].Init(m_images[i], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_ASPECT_COLOR_BIT);
    }
    return true;
}


bool Swapchain::Create(VkSurfaceKHR surface, uint32_t width, uint32_t height, bool vsync) noexcept
{
    if ((surface == VK_NULL_HANDLE) or (vkContext.Device() == VK_NULL_HANDLE))
        return false;

    m_vsync = vsync;
    if (not PickSurfaceFormat(surface))
        return false;
    if (not PickPresentMode(surface, vsync))
        return false;

    // Surface capabilities: clamp extent and pick image count.
    VkSurfaceCapabilitiesKHR caps { };
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vkContext.PhysicalDevice(), surface, &caps);

    // currentExtent {0xFFFFFFFF, 0xFFFFFFFF} means "use whatever you want" — fall back to
    // requested width/height clamped to min/max.
    if (caps.currentExtent.width != UINT32_MAX) {
        m_extent = caps.currentExtent;
    }
    else {
        m_extent.width = std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
        m_extent.height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }

    uint32_t imageCount = caps.minImageCount + 1;  // +1 to avoid waiting on the driver
    if ((caps.maxImageCount > 0) and (imageCount > caps.maxImageCount))
        imageCount = caps.maxImageCount;
    if (imageCount > MAX_BACK_BUFFERS)
        imageCount = MAX_BACK_BUFFERS;

    VkSwapchainCreateInfoKHR info { };
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = surface;
    info.minImageCount = imageCount;
    info.imageFormat = m_format;
    info.imageColorSpace = m_colorSpace;
    info.imageExtent = m_extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    // Queue-family sharing: graphics + present queue. If they're the same family, use exclusive.
    uint32_t qfamilies[2] = { vkContext.GraphicsFamily(), vkContext.PresentFamily() };
    if (qfamilies[0] != qfamilies[1]) {
        info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices = qfamilies;
    }
    else {
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = m_presentMode;
    info.clipped = VK_TRUE;
    info.oldSwapchain = VK_NULL_HANDLE;

    VkResult res = vkCreateSwapchainKHR(vkContext.Device(), &info, nullptr, &m_swapchain);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "Swapchain::Create: vkCreateSwapchainKHR failed (%d)\n", (int)res);
        return false;
    }
    return AcquireImages();
}


void Swapchain::Destroy(void) noexcept
{
    VkDevice device = vkContext.Device();
    if (device == VK_NULL_HANDLE)
        return;

    for (uint32_t i = 0; i < m_imageCount; ++i) {
        if (m_imageViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device, m_imageViews[i], nullptr);
            m_imageViews[i] = VK_NULL_HANDLE;
        }
        m_images[i] = VK_NULL_HANDLE;
        m_layoutTrackers[i] = ImageLayoutTracker { };
    }
    m_imageCount = 0;

    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}


bool Swapchain::Recreate(VkSurfaceKHR surface, uint32_t width, uint32_t height, bool vsync) noexcept
{
    if (vkContext.Device() != VK_NULL_HANDLE)
        vkDeviceWaitIdle(vkContext.Device());
    Destroy();
    return Create(surface, width, height, vsync);
}

// =================================================================================================
