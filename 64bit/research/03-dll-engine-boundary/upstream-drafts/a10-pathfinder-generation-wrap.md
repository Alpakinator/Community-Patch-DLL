# Upstream: the path finder's node-cache generation ID wraps without clearing the node stamps (draft for the owner, nothing sent)

Written 2026-11-17 for ticket 4806 (epic 0504). Rules: D95 (nothing sent upstream carries internal bug or decision ids,
project names or cherry-pick lines); D130 (nothing is sent without the owner's yes to the exact text). What the text rests on:
`../a10-pathfinder-generation-wrap.md`.

## Checked first (2026-11-17 19:05 EST)

Upstream `master` `c722e8ea24` (2026-11-18 00:06 UTC) still has the 16-bit ID, the reset to 1 at `0xFFFF` and the
16-bit node stamp. Searches of LoneGazebo/Community-Patch-DLL's issues and pull requests for "GenerationID",
"pathfinder generation", "generation id", "0xFFFF" and "pathfinder cache" find no report of it; the recent commits to
`CvAStar.cpp` and the open path-finder pull requests do not touch it. So a report is new.

## Proposed form

An issue with the fix in its text (a pull request only if the owner prefers one; the diff is 4 lines). The text below
carries the recommended fix (b), the wider stamp (research file section 6: the smaller diff, no layout change, no new
work; both fixes measured exact and free of cost). The alternative text for fix (a) is at the end. The owner's decision: send
this text as an issue, yes or no (recommended: yes, after the beta like the other upstream drafts, D130).

## Text (D95-clean)

> **Title:** Pathfinder node cache: generation ID wraps without clearing node stamps
>
> `CvAStar::FindPathWithCurrentConfiguration` increments `m_iCurrentGenerationID` (an `unsigned short`) before each
> search and sets it back to 1 when it reaches `0xFFFF`. The nodes' `CvPathNodeCacheData::iGenerationID` stamps are not
> cleared at that point. `UpdateNodeCacheData` skips the refresh of a node whose stamp equals the current ID, so a node
> that was stamped by search N of one cycle and is not touched again (neither restamped nor cleared by `Reset()`,
> which clears only the previous search's open and closed nodes) until search N of the next cycle keeps cache data
> that is 65,534 searches old: visibility, revealed state, enemy and neutral units, stacking, territory and terrain
> entry, cities, routes, movement cost multipliers and the airlift/sealift/port flags.
>
> How often: the ID starts at 0 when the DLL is loaded and is not saved, so the wraps depend on how many searches the
> session has made, not on the game turn. In a late-game Huge game the unit path finder makes about 21,000 searches
> per AI turn, so it wraps about every third turn; a Standard game in the 150s wraps about every 17-19 turns. We
> counted the stale case directly (a shadow 32-bit generation per node) over 5 wraps of the unit path finder (about
> 243 million cache compares) and saw it 0 times, so it is rare: a node has to be left alone for exactly 65,534
> searches and then be reached by the one search whose ID matches. It is still a correctness hole in long sessions,
> and the fix costs nothing.
>
> Suggested fix: make the ID and the stamp 32-bit. The stamp then takes the two padding bytes after it, so
> `CvPathNodeCacheData` keeps its size (24 bytes on x86 and x64), and a wrap needs about four billion searches of one
> finder:
>
> ```diff
> --- a/CvGameCoreDLL_Expansion2/CvAStarNode.h
> +++ b/CvGameCoreDLL_Expansion2/CvAStarNode.h
>  	//tell us when to update the cache ...
> -	unsigned short iGenerationID;
> +	unsigned int iGenerationID;
> --- a/CvGameCoreDLL_Expansion2/CvAStar.h
> +++ b/CvGameCoreDLL_Expansion2/CvAStar.h
> -	inline unsigned short GetCurrentGenerationID() const
> +	inline unsigned int GetCurrentGenerationID() const
> ...
> -	unsigned short m_iCurrentGenerationID;		// the cache in each node is tagged with a generation ID which is incremented for each call
> +	unsigned int m_iCurrentGenerationID;		// the cache in each node is tagged with a generation ID which is incremented for each call
> --- a/CvGameCoreDLL_Expansion2/CvAStar.cpp
> +++ b/CvGameCoreDLL_Expansion2/CvAStar.cpp
>  	m_iCurrentGenerationID++;
> -	if (m_iCurrentGenerationID==0xFFFF)
> +	if (m_iCurrentGenerationID==0) //32 bits: wraps only after four billion searches; 0 is the stamp of a cleared node
>  		m_iCurrentGenerationID = 1;
> ```
>
> We ran this change in AI games on Standard and Huge maps, 32-bit and 64-bit builds: the games are identical turn for
> turn to the unchanged DLL's (none of them hit the stale case), the 32-bit and 64-bit builds play the same game, and
> the time per turn did not change measurably.
>
> (An alternative that keeps 16 bits: clear every node's stamp, in both layers of `CvTwoLayerPathFinder`, when the ID
> is set back to 1. It is about 25 lines with a virtual method for the second layer, and costs one pass over the node
> arrays every 65,534 searches.)

What the sentence "We ran this change ..." rests on (research file sections 3-5): identical `stats.db` against the
unchanged DLL on Standard 150-160 and 150-200, Huge 300-305 and a new Standard game 0-50 (64-bit); the save test
crossing the 32-bit and 64-bit fixed builds on tiny 0-100, standard 0-200 and huge 350-400; the time per turn from the
development series on the quiet test machine (section 5: its clause stays only if the series shows no cost).

## Alternative text for fix (a) (only if the recommendation changes)

Replace the "Suggested fix" paragraph and the diff by: "Suggested fix: when the ID is set back to 1, set every node's
stamp to 0 (the stamp of a cleared node, which no ID uses), in both layers of `CvTwoLayerPathFinder`." and the diff of
fork branch `fix-a10-x64-clear` (the research file, section 2).
