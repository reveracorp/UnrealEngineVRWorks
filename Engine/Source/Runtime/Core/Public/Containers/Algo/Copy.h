// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

namespace Algo
{
	/**
	 * Conditionally copies a range into a container
	 *
	 * @param  Input  Any iterable type
	 * @param  Output  Container to hold the output
	 * @param  Predicate  Condition which returns true for elements that should be copied and false for elements that should be skipped
	 */
	template <typename InT, typename OutT, typename PredicateT>
	FORCEINLINE void CopyIf(const InT& Input, OutT& Output, PredicateT Predicate)
	{
		for (auto&& Value : Input)
		{
			if (Predicate(Value))
			{
				Output.Add(Value);
			}
		}
	}

	/**
	 * Copies a range into a container
	 *
	 * @param  Input  Any iterable type
	 * @param  Output  Container to hold the output
	 */
	template <typename InT, typename OutT>
	FORCEINLINE void Copy(const InT& Input, OutT& Output)
	{
		for (auto&& Value : Input)
		{
			Output.Add(Value);
		}
	}
}
