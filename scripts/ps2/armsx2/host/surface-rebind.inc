// SPDX-License-Identifier: GPL-3.0-or-later
// Private derivative for ARMSX2. Original 3DS files/behavior are unchanged.
namespace emuorbit::n3ds {
bool VulkanRenderHost::suspendPresentation(std::string& error) {
    if (!isOwnerThread()) return fail(error, "Surface suspend requires native owner");
    if (device_ == VK_NULL_HANDLE) return fail(error, "No negotiated Vulkan device");
    if (!window_) return true;
    const auto waitIdle = loadDevice<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle", error);
    const auto destroyFence = loadDevice<PFN_vkDestroyFence>("vkDestroyFence", error);
    const auto destroySemaphore = loadDevice<PFN_vkDestroySemaphore>("vkDestroySemaphore", error);
    const auto destroyPool = loadDevice<PFN_vkDestroyCommandPool>("vkDestroyCommandPool", error);
    const auto destroySwapchain = loadDevice<PFN_vkDestroySwapchainKHR>("vkDestroySwapchainKHR", error);
    const auto destroySurface = loadInstance<PFN_vkDestroySurfaceKHR>("vkDestroySurfaceKHR", error);
    if (!waitIdle || !destroyFence || !destroySemaphore || !destroyPool || !destroySwapchain || !destroySurface)
        return false;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        // The caller has already confirmed CPU/VU/GS pause. A driver hang here
        // is NOT a confirmed teardown; the host must quarantine, not free/retry.
        if (waitIdle(device_) != VK_SUCCESS) return fail(error, "Unconfirmed Vulkan idle during Surface loss");
        if (frameFence_) destroyFence(device_, frameFence_, nullptr);
        if (renderFinished_) destroySemaphore(device_, renderFinished_, nullptr);
        if (imageAvailable_) destroySemaphore(device_, imageAvailable_, nullptr);
        if (commandPool_) destroyPool(device_, commandPool_, nullptr);
        if (swapchain_) destroySwapchain(device_, swapchain_, nullptr);
        if (surface_) destroySurface(instance_, surface_, nullptr);
    }
    frameFence_ = VK_NULL_HANDLE; renderFinished_ = VK_NULL_HANDLE; imageAvailable_ = VK_NULL_HANDLE;
    commandPool_ = VK_NULL_HANDLE; commandBuffer_ = VK_NULL_HANDLE;
    swapchain_ = VK_NULL_HANDLE; surface_ = VK_NULL_HANDLE;
    swapchainImages_.clear(); swapchainImageInitialized_.clear(); extent_ = {};
    frameAcquired_ = false; syncIndex_ = 0; report_.swapchainImageCount = 0;
    {
        std::lock_guard<std::mutex> lock(frameMutex_);
        coreImage_ = nullptr; coreWaitSemaphores_.clear(); coreCommandBuffers_.clear();
        coreSourceQueueFamily_ = VK_QUEUE_FAMILY_IGNORED; coreSignalSemaphore_ = VK_NULL_HANDLE;
    }
    ANativeWindow_release(window_); window_ = nullptr;
    return true;
}

bool VulkanRenderHost::replacePresentation(ANativeWindow* window, uint32_t width,
        uint32_t height, std::string& error) {
    if (!isOwnerThread()) return fail(error, "Surface replacement requires native owner");
    if (!window || !width || !height || width > 8192 || height > 8192)
        return fail(error, "Invalid replacement Surface/extent");
    if (window_ || swapchain_ || surface_ || frameAcquired_ || device_ == VK_NULL_HANDLE)
        return fail(error, "Replacement requires paused, suspended presentation");
    window_ = window; ANativeWindow_acquire(window_);
    if (!createSurface(error)) return false; // Stay paused; partial resources owned by this host.
    const auto supports = loadInstance<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(
            "vkGetPhysicalDeviceSurfaceSupportKHR", error);
    VkBool32 compatible = VK_FALSE;
    if (!supports || supports(gpu_, presentationQueueFamilyIndex_, surface_, &compatible) != VK_SUCCESS || !compatible)
        return fail(error, "New Surface incompatible with existing presentation queue");
    if (!createSwapchain(width, height, error) || !createFrameResources(error)) return false;
    report_.swapchainImageCount = static_cast<uint32_t>(swapchainImages_.size());
    return true;
}
} // namespace emuorbit::n3ds
