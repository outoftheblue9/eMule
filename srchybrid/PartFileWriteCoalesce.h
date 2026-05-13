//this file is part of eMule
//Copyright (C)2020-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
#pragma once

#include <cstdint>
#include <vector>

// Pure helper for coalescing contiguous buffered fragments into single I/O runs.
// Lives in its own translation unit (no MFC, no Windows headers) so the
// emule_tests unit-test project can compile and exercise it standalone.

struct MergeFragment
{
	uint64_t start;
	uint64_t end;     // inclusive
	const void *data; // NULL => unmergeable (allocation marker)
};

struct MergedRun
{
	std::size_t firstIdx; // index of first source fragment in the input vector
	std::size_t count;    // number of fragments covered, [firstIdx, firstIdx+count)
};

// Build runs of contiguous fragments. Input must be sorted by `end` ascending
// and non-overlapping. A fragment is merged into the previous run when:
//   - both fragments have non-null `data`
//   - both fragments have end >= start (length > 0)
//   - prev.end + 1 == next.start
// Allocation-marker fragments (data==NULL) and zero-length fragments
// (end < start) always emit as count==1 runs and never merge with neighbours.
void CoalesceContiguous(const std::vector<MergeFragment> &in,
                        std::vector<MergedRun> &out);
