#ifndef POLYMER_ASSET_BLOCK_MODEL_ROTATE_H_
#define POLYMER_ASSET_BLOCK_MODEL_ROTATE_H_

#include <polymer/asset/parsed_block_model.h>

namespace polymer {
namespace asset {

void RotateVariant(MemoryArena& perm_arena, world::BlockModel& model, const ParsedBlockModel& parsed_model,
                   size_t element_start, size_t element_count, const Vector3i& rotation, bool uvlock);

} // namespace asset
} // namespace polymer

#endif
