#pragma once

#include "CommandContext.h"
#include "VulkanContext.h"

// Allocates, begins, and later submits+waits-on a one-off primary command
// buffer from CommandContext's pool. Used for anything that needs to record
// a handful of GPU commands and block until they complete: staged buffer/
// image uploads, and (from M1 onward) acceleration-structure builds.
VkCommandBuffer beginSingleTimeCommands(VulkanContext& ctx, CommandContext& commands);
void endSingleTimeCommands(VulkanContext& ctx, CommandContext& commands, VkCommandBuffer cmd);
