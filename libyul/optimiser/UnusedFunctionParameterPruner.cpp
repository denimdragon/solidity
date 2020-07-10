/*
	This file is part of solidity.

	solidity is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	solidity is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with solidity.  If not, see <http://www.gnu.org/licenses/>.
*/
// SPDX-License-Identifier: GPL-3.0
/**
 * UnusedFunctionParameterPruner: Optimiser step that removes unused parameters from function
 * definition.
 */

#include <libyul/optimiser/UnusedFunctionParameterPruner.h>
#include <libyul/optimiser/UnusedFunctionsCommon.h>
#include <libyul/optimiser/OptimiserStep.h>
#include <libyul/optimiser/NameCollector.h>
#include <libyul/optimiser/NameDisplacer.h>
#include <libyul/optimiser/NameDispenser.h>
#include <libyul/YulString.h>
#include <libyul/AsmData.h>
#include <libyul/Dialect.h>

#include <libsolutil/CommonData.h>

#include <algorithm>
#include <optional>
#include <variant>

using namespace std;
using namespace solidity::util;
using namespace solidity::yul;

namespace
{

bool anyFalse(vector<bool> const& _mask)
{
	return any_of(_mask.begin(), _mask.end(), [](bool b){ return !b; });
}

} // anonymous namespace

void UnusedFunctionParameterPruner::run(OptimiserStepContext& _context, Block& _block)
{

	map<YulString, size_t> references = ReferencesCounter::countReferences(_block);
	auto used = [&](auto v) -> bool { return references.count(v.name); };

	// Function name and a boolean mask, where `false` at index `i` indicates that the function
	// argument at index `i` in `FunctionDefinition::parameters` is unused inside the function body.
	map<YulString, vector<bool>> unusedParameters;
	// Function name and a boolean mask, where `false` at index `i` indicates that the
	// return-parameter at index `i` in `FunctionDefinition::returnVariables` is unused inside the
	// function body.
	map<YulString, vector<bool>> unusedReturnVariables;

	// Step 1 of UnusedFunctionParameterPruner: Find functions whose parameters (both arguments and
	// return-parameters) are not used in its body.
	for (auto const& statement: _block.statements)
		if (holds_alternative<FunctionDefinition>(statement))
		{
			FunctionDefinition const& f = std::get<FunctionDefinition>(statement);

			if (wasPruned(f, _context.dialect))
				continue;

			vector<bool> parameters = applyMap(f.parameters, used);
			vector<bool> returnVariables = applyMap(f.returnVariables, used);

			if (anyFalse(parameters))
				unusedParameters[f.name] = move(parameters);
			if (anyFalse(returnVariables))
				unusedReturnVariables[f.name] = move(returnVariables);
		}

	set<YulString> namesToFree = util::keys(unusedParameters) + util::keys(unusedReturnVariables);

	// Step 2 of UnusedFunctionParameterPruner: Replace all references of the function, say `f`, by
	// a new name, say `f_1`.
	NameDisplacer replace{_context.dispenser, namesToFree};
	replace(_block);
	// Inverse-Map of the above translations. In the above example, this will store an element with
	// key `f_1` and value `f`.
	std::map<YulString, YulString> inverseTranslations = invertMap(replace.translations());

	// Step 3 of UnusedFunctionParameterPruner: introduce a new function in the block with body of
	// the old one. Replace the body of the old one with a function call to the new one with reduced
	// parameters.
	//
  	// For example: introduce a new function `f` with the same the body as `f_1`, but with reduced
	// parameters, i.e., `function f() -> y { y := 1 }`. Now replace the body of `f_1` with a call to
	// `f`, i.e., `f_1(x) -> y { y := f() }`.
	iterateReplacing(_block.statements, [&](Statement& _s) -> optional<vector<Statement>>
	{
		if (holds_alternative<FunctionDefinition>(_s))
		{
			FunctionDefinition& old = get<FunctionDefinition>(_s);
			if (inverseTranslations.count(old.name))
			{
				FunctionDefinition replacement = createReplacement(
					old,
					unusedParameters,
					unusedReturnVariables,
					_context.dispenser,
					inverseTranslations
				);

				return make_vector<Statement>(move(replacement), move(old));
			}
		}

		return nullopt;
	});
}
