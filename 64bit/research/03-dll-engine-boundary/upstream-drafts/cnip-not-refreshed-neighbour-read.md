# Upstream: unit path searches read node cache data they have not refreshed (draft for the owner, nothing sent)

Written 2026-11-18 for ticket 4813 (epic 0504). Rules: D95 (nothing sent upstream carries internal bug or decision ids,
project names or cherry-pick lines); D130 (nothing is sent without the owner's yes to the exact text). What the text rests on:
`../cnip-not-refreshed-neighbour-read.md` (sections 1, 2, 4, 5).

## Checked first (2026-11-18, before 13:02 EST)

Upstream `master` `3a511df0f` (2026-11-17 23:44 EST) has the two reads unchanged (`CommonNeighborIsPassable`,
`CreateChildren`'s extra-children loop, `NodeAddedToPath`, `PathCost`). Searches of LoneGazebo/Community-Patch-DLL's
issues and pull requests for "CommonNeighborIsPassable", "UpdateNodeCacheData", "pathfinder airlift", "pathfinder
nondeterministic", "pathfinder stale cache", "path finder cache neighbor", "APPROX_TARGET_RING2", "pathfinder history"
and "airlift path" find no report of it; the open pull requests touching `CvAStar*` (#13518, #13424) do not touch these
lines. So a report is new.

## Proposed form

A pull request with the 20-line diff and the text below (the change is small and self-contained; an issue first only if
the owner prefers). Unlike the generation-ID report, this one changes AI decisions in normal games, so the text says so
plainly. The owner's decision: send this text as a pull request, yes or no (recommended: yes, after the beta like the other
upstream drafts, D130). Every measured sentence was settled by the results of 2026-11-18 (research file sections 4-6).

## Text (D95-clean)

> **Title:** Pathfinder: update the node cache of a common neighbour and of extra children before reading it
>
> A unit path search reads per-node data (`CvPathNodeCacheData`) that `UpdateNodeCacheData` fills for the searching
> unit and stamps with the search's generation ID. Two places read it from nodes that the current search may not have
> updated yet, so they use whatever an earlier search left there (often a search for another unit) or the zeroes that
> `Reset()` wrote:
>
> 1. `CvTwoLayerPathFinder::CommonNeighborIsPassable`, used by `DestinationReached` for `MOVEFLAG_APPROX_TARGET_RING2`.
>    `PathCost()` evaluates `IsPathDest(to)` before `NodeAddedToPath()` updates the neighbours of `to`, and the common
>    neighbour of `to` and the target is two steps from the node being expanded. If the stale flags say "impassable",
>    `to` is not taken as the destination in `PathCost()`, gets the end-turn cost, and the search can return a path that
>    is turns longer (we saw 3 turns instead of 0 for an army unit's `TurnsToReachTarget` to its muster plot); if they
>    say "passable" for a plot this unit cannot cross, the opposite.
> 2. `CvAStar::CreateChildren`, the unit path finder's extra children (airlift targets, admiral port changes).
>    `NodeAddedToPath()` updates only the extra children `GetExtraChildren()` returned when the node was added. The set
>    at expansion can be different: an airlift needs the node's moves to be full or zero, and an open node's moves change
>    when a cheaper path reaches it. `PathValid()` and `PathCost()` then judge those children on stale data.
>
> The visible effect is that the answer to a path query depends on which path queries ran before it, so caching or
> skipping an unrelated query (or any change in the order of AI queries) can change a later AI decision. We found it
> while checking that a cache of path results gives exactly the same game: it did not, and every difference traced back
> to these two reads.
>
> Fix: update the node before reading it. `UpdateNodeCacheData` returns at once for a node that is already current, and
> the game does not change during a search, so the only change is that these reads use the searching unit's own
> current data:
>
> ```diff
> --- a/CvGameCoreDLL_Expansion2/CvAStar.cpp
> +++ b/CvGameCoreDLL_Expansion2/CvAStar.cpp
> +void UpdateNodeCacheData(CvAStarNode* node, const CvUnit* pUnit, const CvAStar* finder);
> +
>  //	--------------------------------------------------------------------------------
>  /// Creates children for the node
>  void CvAStar::CreateChildren(CvAStarNode* node)
> @@ (extra children)
>  				SetChangePortCitiesChecked(true);
>  		}
> +
> +		//NodeAddedToPath updated the cache only for the extra children found when this node was added. the set can be
> +		//different now (eg airlifts need full or zero moves), so update the cache of a unit path's extra children here,
> +		//otherwise PathValid and PathCost may read data left over from an earlier search (possibly for another unit)
> +		const CvUnit* pUnit = NULL;
> +		if (!extraChildren.empty() && udGetExtraChildrenFunc == UnitPathGetExtraChildren)
> +			pUnit = GET_PLAYER(m_sData.ePlayer).getUnit(m_sData.iUnitID);
> +
>  		for(size_t i = 0; i < extraChildren.size(); i++)
>  		{
> @@
>  				if (check == node->m_pParent)
>  					continue;
>  
> +				UpdateNodeCacheData(check, pUnit, this); //does nothing if pUnit is null or the data is current
> +
>  				if (udFunc(udValid, node, check, m_sData))
> @@ bool CvTwoLayerPathFinder::CommonNeighborIsPassable(const CvAStarNode * a, const CvAStarNode * b) const
>  		CvAStarNode* check = a->m_apNeighbors[i];
>  		if (check && plotDistance(check->m_iX, check->m_iY, b->m_iX, b->m_iY) == 1)
> +		{
> +			//the common neighbor can be two steps away from the node being expanded (PathCost checks the destination
> +			//before NodeAddedToPath updates the neighbors of the new node), so its cache data may be left over from an
> +			//earlier search. update it first (does nothing if it is current)
> +			UpdateNodeCacheData(check, reinterpret_cast<const UnitPathCacheData*>(GetScratchBuffer())->pUnit, this);
>  			if (check->m_kCostCacheData.bCanEnterTerrainIntermediate && check->m_kCostCacheData.bCanEnterTerritoryIntermediate)
>  				return true;
> +		}
> --- a/CvGameCoreDLL_Expansion2/CvAStar.h
> +++ b/CvGameCoreDLL_Expansion2/CvAStar.h
> -int UnitPathGetExtraChildren(const CvAStarNode* node, CvAStar* finder, vector<pair<int, int>>& out);
> +int UnitPathGetExtraChildren(const CvAStarNode* node, const CvAStar* finder, vector<pair<int, int>>& out);
> ```
>
> (The header line only makes the declaration match the definition, which takes `const CvAStar*`.)
>
> What we checked: with the change, a per-search trace of the unit path finder finds no read of not-updated node data
> left at any site in AI games on Standard (turns 0-50, 50-200, 150-160) and Huge (300-305) maps, and the answers no
> longer depend on which earlier queries ran. It changes AI decisions, so games diverge from the unchanged DLL: a
> fixed-seed Tiny game from turn 43, a Standard one from turn 50, a Huge late game continued from turn 350 from turn 355.
> 32-bit and 64-bit builds play the same game with it, and their saves load in each other. The extra work is small:
> on a late-game Huge map about 2,300 extra checks per AI turn at the first site and 430,000 at the second, of which
> about 3,000 actually update a node; on a quiet machine the time per AI turn on that map did not change measurably
> (within about 0.5 % on the turn both builds play alike).

What the sentences rest on (research file): the trace (section 4), the window games and the D10 games (section 5), the
save test (section 6), the counts and the pair (section 6). The "3 turns instead of 0" example is
`../../08-performance/speedup-danger.md` 8.2c (181:1613).
