#pragma once

#include "vkframework.h"
#include "image_layout_tracker.h"

// =================================================================================================
// Swapchain — Vulkan VkSwapchainKHR wrapper.
//
// Encapsulates the Vulkan analogue of the DX12 IDXGISwapChain3 plus its back buffers:
//   • VkSwapchainKHR
//   • per-image VkImage handles (owned by the swapchain — destroyed by vkDestroySwapchainKHR)
//   • per-image VkImageView (color attachment view, we own and destroy)
//   • per-image ImageLayoutTracker (initial UNDEFINED → COLOR_ATTACHMENT on first use, →
//     PRESENT_SRC_KHR before vkQueuePresentKHR)
//
// Format / present-mode / image count are picked at Create time from the surface capabilities
// (VK_PRESENT_MODE_FIFO_KHR with vsync, MAILBOX/IMMEDIATE without; VK_FORMAT_B8G8R8A8_UNORM
// preferred). Recreate handles window resize: WaitIdle on the device, destroy views, recreate
// swapchain with the new extent, re-acquire image views.
//
// Owned by BaseDisplayHandler. Sync objects (semaphores / fence per frame slot) live in the
// CommandQueue / FrameSync wrapper, not here.

class Swapchain
{
public:
    static constexpr uint32_t MAX_BACK_BUFFERS = 4;

    VkSwapchainKHR     m_swapchain { VK_NULL_HANDLE };
    VkFormat           m_format    { VK_FORMAT_B8G8R8A8_UNORM };
    VkColorSpaceKHR    m_colorSpace { VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
    VkPresentModeKHR   m_presentMode { VK_PRESENT_MODE_FIFO_KHR };
    VkExtent2D         m_extent    { 0, 0 };

    uint32_t           m_imageCount { 0 };
    VkImage            m_images[MAX_BACK_BUFFERS]      { };
    VkImageView        m_imageViews[MAX_BACK_BUFFERS]  { };
    ImageLayoutTracker m_layoutTrackers[MAX_BACK_BUFFERS];

    bool               m_vsync { true };

    // Creates the swapchain on the given surface with the given window dimensions.
    // Picks format / present mode / image count from VkSurfaceCapabilitiesKHR.
    bool Create(VkSurfaceKHR surface, uint32_t width, uint32_t height, bool vsync) noexcept;

    // Destroys image views + swapchain. m_images become invalid (owned by the swapchain).
    void Destroy(void) noexcept;

    // Resize-on-resize handler: WaitIdle, Destroy, Create with new extent.
    bool Recreate(VkSurfaceKHR surface, uint32_t width, uint32_t height, bool vsync) noexcept;

    inline VkSwapchainKHR Handle(void) const noexcept { return m_swapchain; }
    inline VkFormat Format(void) const noexcept { return m_format; }
    inline VkExtent2D Extent(void) const noexcept { return m_extent; }
    inline uint32_t ImageCount(void) const noexcept { return m_imageCount; }
    inline VkImage Image(uint32_t i) const noexcept { return m_images[i]; }
    inline VkImageView ImageView(uint32_t i) const noexcept { return m_imageViews[i]; }
    inline ImageLayoutTracker& LayoutTracker(uint32_t i) noexcept { return m_layoutTrackers[i]; }

private:
    bool PickSurfaceFormat(VkSurfaceKHR surface) noexcept;
    bool PickPresentMode(VkSurfaceKHR surface, bool vsync) noexcept;
    bool AcquireImages(void) noexcept;
};

// =================================================================================================
