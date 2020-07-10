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
#pragma once

#include <libyul/optimiser/NameDispenser.h>
#include <libyul/AsmData.h>
#include <libyul/Dialect.h>
#include <libyul/Exceptions.h>

#include <liblangutil/SourceLocation.h>

#include <libsolutil/CommonData.h>

#include <variant>

namespace solidity::yul
{

template<typename T>
std::vector<T> applyBooleanMask(std::vector<T> const& _vec, std::vector<bool> const& _mask)
{
	yulAssert(_vec.size() == _mask.size(), "");

	std::vector<T> ret;

	for (size_t i = 0; i < _mask.size(); ++i)
		if (_mask[i])
			ret.push_back(_vec[i]);

	return ret;
}

/// Find functions whose arguments are not used in its body. Also, find functions whose body
/// satisfies a heuristic about pruning.
bool wasPruned(FunctionDefinition const& _f, Dialect const& _dialect)
{
	// We skip the function body if it
	// 1. is empty, or
	// 2. is a single statement that is an assignment statement whose value is a non-builtin
	//    function call, or
	// 3. is a single expression-statement that is a non-builtin function call.
	// The above cases are simple enough so that the inliner alone can remove the parameters.
	if (_f.body.statements.empty())
		return true;
	if (_f.body.statements.size() == 1)
	{
		Statement const& e = _f.body.statements[0];
		if (std::holds_alternative<Assignment>(e))
		{
			if (std::holds_alternative<FunctionCall>(*std::get<Assignment>(e).value))
			{
				FunctionCall c = std::get<FunctionCall>(*std::get<Assignment>(e).value);
				if (!_dialect.builtin(c.functionName.name))
					return true;
			}
		}
		else if (std::holds_alternative<ExpressionStatement>(e))
			if (std::holds_alternative<FunctionCall>(std::get<ExpressionStatement>(e).expression))
			{
				FunctionCall c = std::get<FunctionCall>(std::get<ExpressionStatement>(e).expression);
				if (!_dialect.builtin(c.functionName.name))
					return true;
			}
	}
	return false;
}

FunctionDefinition createReplacement(
	FunctionDefinition& _old,
	std::map<YulString, std::vector<bool>> const& _unusedParameters,
	std::map<YulString, std::vector<bool>> const& _unusedReturnVariables,
	NameDispenser& _nameDispenser,
	std::map<YulString, YulString> _inverseTranslations
)
{
	auto generateName = [&](TypedName t)
	{
		return TypedName{
			t.location,
			_nameDispenser.newName(t.name),
			t.type
		};
	};

	langutil::SourceLocation loc = _old.location;
	YulString newName = _inverseTranslations.at(_old.name);
	TypedNameList functionArguments;
	TypedNameList returnVariables;
	TypedNameList renamedParameters = util::applyMap(_old.parameters, generateName);
	TypedNameList reducedRenamedParameters;
	TypedNameList renamedReturnVariables = util::applyMap(_old.returnVariables, generateName);
	TypedNameList reducedRenamedReturnVariables;

	if (_unusedParameters.count(newName))
	{
		std::vector<bool> const& mask = _unusedParameters.at(newName);
		functionArguments = applyBooleanMask(_old.parameters, mask);
		reducedRenamedParameters = applyBooleanMask(renamedParameters, mask);
	}
	else
	{
		functionArguments = _old.parameters;
		reducedRenamedParameters = renamedParameters;
	}

	if (_unusedReturnVariables.count(newName))
	{
		std::vector<bool> const& mask = _unusedReturnVariables.at(newName);
		returnVariables = applyBooleanMask(_old.returnVariables, mask);
		reducedRenamedReturnVariables = applyBooleanMask(renamedReturnVariables, mask);
	}
	else
	{
		returnVariables = _old.returnVariables;
		reducedRenamedReturnVariables = renamedReturnVariables;
	}

	FunctionDefinition newFunction{
		loc,
		std::move(newName),
		std::move(functionArguments),
		std::move(returnVariables),
		{loc, {}} // body
	};

	std::swap(newFunction.body, _old.body);
	std::swap(_old.parameters, renamedParameters);
	std::swap(_old.returnVariables, renamedReturnVariables);

	FunctionCall call{loc, Identifier{loc, newFunction.name}, {}};
	for (auto const& p: reducedRenamedParameters)
		call.arguments.emplace_back(Identifier{loc, p.name});

	// Replace the body of `f_1` by an assignment which calls `f`, i.e.,
	// `return_parameters = f(reduced_parameters)`
	if (!newFunction.returnVariables.empty())
	{
		Assignment assignment;
		assignment.location = loc;

		// The LHS of the assignment.
		for (auto const& r: reducedRenamedReturnVariables)
			assignment.variableNames.emplace_back(Identifier{loc, r.name});

		assignment.value = std::make_unique<Expression>(std::move(call));

		_old.body.statements.emplace_back(std::move(assignment));
	}
	else
		_old.body.statements.emplace_back(ExpressionStatement{loc, std::move(call)});

	return newFunction;
}


}
