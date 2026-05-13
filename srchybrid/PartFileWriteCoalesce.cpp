//this file is part of eMule
//Copyright (C)2020-2026 Merkur ( strEmail.Format("%s@%s", "devteam", "emule-project.net") / https://www.emule-project.net )
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
#include "PartFileWriteCoalesce.h"

void CoalesceContiguous(const std::vector<MergeFragment> &in,
                        std::vector<MergedRun> &out)
{
	out.clear();
	if (in.empty())
		return;

	auto isMergeable = [](const MergeFragment &f) {
		return f.data != nullptr && f.end >= f.start;
	};

	std::size_t i = 0;
	while (i < in.size()) {
		MergedRun run{i, 1};
		if (isMergeable(in[i])) {
			while (i + run.count < in.size()) {
				const MergeFragment &prev = in[i + run.count - 1];
				const MergeFragment &next = in[i + run.count];
				if (!isMergeable(next))
					break;
				if (prev.end + 1 != next.start)
					break;
				++run.count;
			}
		}
		out.push_back(run);
		i += run.count;
	}
}
