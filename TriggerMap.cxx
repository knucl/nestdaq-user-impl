/*
 *
 *
 */

#ifndef TriggerMap_cxx
#define TriggerMap_cxx

#include <iostream>
#include <iomanip>
#include <string>
#include <vector>
#include <stack>
#include <algorithm>
#include <cctype>
#include <memory>
#include <stdexcept>

#include "LogiCalc.cxx"

class TriggerMap
{
public:
	TriggerMap(){};
	virtual ~TriggerMap(){};
	void MakeTable(std::string &);
	bool LookUp(uint32_t);
	uint32_t LookUpOrFlags(uint32_t);
	unsigned int GetOrTermCount() const { return fOrTerms.size(); }
	void Dump();
protected:
private:
	struct ExpressionNode {
		std::string value;
		std::shared_ptr<ExpressionNode> left;
		std::shared_ptr<ExpressionNode> right;
	};

	bool ExtractBit(uint32_t, int);
	std::shared_ptr<ExpressionNode> BuildExpressionTree(
		const std::vector<std::string> &commands);
	void FlattenTopLevelOr(
		const std::shared_ptr<ExpressionNode> &node,
		std::vector<std::shared_ptr<ExpressionNode>> &terms);
	bool EvaluateExpression(
		const std::shared_ptr<ExpressionNode> &node,
		uint32_t value);
	std::vector<bool> fLut;
	std::vector<uint32_t> fOrFlagsLut;
	std::vector<std::shared_ptr<ExpressionNode>> fOrTerms;
	unsigned int fNsignal = 0;
	unsigned int fMapSize = 0;
};

bool TriggerMap::ExtractBit(uint32_t val, int digit)
{
	if (digit < 32) {
		uint32_t bit = (val >> digit) & 0x00000001;
		return (bit > 0) ? true : false;
	} else {
		return false;
	}
}

std::shared_ptr<TriggerMap::ExpressionNode> TriggerMap::BuildExpressionTree(
	const std::vector<std::string> &commands)
{
	std::stack<std::shared_ptr<ExpressionNode>> nodes;

	for (const auto &command : commands) {
		if (! command.empty()
			&& std::all_of(command.cbegin(), command.cend(),
				[](unsigned char c) { return std::isdigit(c); })) {
			auto node = std::make_shared<ExpressionNode>();
			node->value = command;
			nodes.push(node);
			continue;
		}

		if (command == "!") {
			if (nodes.empty()) {
				throw std::runtime_error("invalid trigger expression before '!'");
			}
			auto node = std::make_shared<ExpressionNode>();
			node->value = command;
			node->left = nodes.top();
			nodes.pop();
			nodes.push(node);
			continue;
		}

		if ((command == "&") || (command == "|")) {
			if (nodes.size() < 2) {
				throw std::runtime_error(
					"invalid trigger expression before '" + command + "'");
			}
			auto right = nodes.top();
			nodes.pop();
			auto left = nodes.top();
			nodes.pop();
			auto node = std::make_shared<ExpressionNode>();
			node->value = command;
			node->left = left;
			node->right = right;
			nodes.push(node);
			continue;
		}

		throw std::runtime_error(
			"OR flag extraction does not support operator '" + command + "'");
	}

	if (nodes.size() != 1) {
		throw std::runtime_error("invalid trigger expression result stack");
	}
	return nodes.top();
}

void TriggerMap::FlattenTopLevelOr(
	const std::shared_ptr<ExpressionNode> &node,
	std::vector<std::shared_ptr<ExpressionNode>> &terms)
{
	if (node && (node->value == "|")) {
		FlattenTopLevelOr(node->left, terms);
		FlattenTopLevelOr(node->right, terms);
	} else if (node) {
		terms.emplace_back(node);
	}
}

bool TriggerMap::EvaluateExpression(
	const std::shared_ptr<ExpressionNode> &node,
	uint32_t value)
{
	if (! node) {
		return false;
	}
	if (! node->value.empty()
		&& std::all_of(node->value.cbegin(), node->value.cend(),
			[](unsigned char c) { return std::isdigit(c); })) {
		return ExtractBit(value, std::stoi(node->value));
	}
	if (node->value == "!") {
		return ! EvaluateExpression(node->left, value);
	}
	if (node->value == "&") {
		return EvaluateExpression(node->left, value)
			&& EvaluateExpression(node->right, value);
	}
	if (node->value == "|") {
		return EvaluateExpression(node->left, value)
			|| EvaluateExpression(node->right, value);
	}
	return false;
}

void TriggerMap::MakeTable(std::string & formula)
{
	LogiCalc calc;
	auto commands = calc.SetFormula(formula);

	fOrTerms.clear();
	try {
		auto expression = BuildExpressionTree(commands);
		FlattenTopLevelOr(expression, fOrTerms);
	} catch (const std::exception &error) {
		std::cerr << "#E Failed to prepare OR flags: " << error.what()
			<< std::endl;
		throw;
	}
	if (fOrTerms.size() > 24) {
		throw std::runtime_error(
			"trigger-expression has more than 24 top-level OR terms");
	}
	std::cout << "# Trigger OR terms: " << fOrTerms.size() << std::endl;

	fNsignal = calc.GetSigMax() + 1;
	fMapSize = 0x1 << fNsignal;
	fLut.resize(fMapSize);
	fOrFlagsLut.resize(fMapSize);


	for (uint32_t i = 0 ; i < fMapSize ; i++) {
	
		#if 0
		bool bval = true;
		for (unsigned int j = 0 ;  j < fNsignal ; j++) {
			bval =  bval && ExtractBit(i, j);
		}
		fLut[i] = bval;
		#endif

		#if 0
		bool bval = false;
		for (unsigned int j = 0 ;  j < (fNsignal / 2) ; j++) {
			bval |= ExtractBit(i, (j * 2)) && ExtractBit(i, (j * 2) + 1);
		}
		fLut[i] = bval;
		#endif

		#if 0
		bool bval = false;
		for (unsigned int j = 0 ;  j < fNsignal ; j++) {
			bval |= ExtractBit(i, j);
		}
		fLut[i] = bval;
		#endif

		#if 0
		bool dtof = false;
		bool utof = false;
		for (unsigned int j = 0 ;  j < 3 ; j++) {
			dtof |= ExtractBit(i, (j * 2)) && ExtractBit(i, (j * 2) + 1);
		}
		for (unsigned int j = 0 ;  j < 2 ; j++) {
			utof |= ExtractBit(i, (j * 2) + 6) && ExtractBit(i, (j * 2) + 6 + 1);
		}
		fLut[i] = utof && dtof;
		#endif

		fLut[i] = calc.Calc(i);
		uint32_t orFlags = 0;
		if (fLut[i]) {
			for (size_t term = 0; term < fOrTerms.size(); ++term) {
				if (EvaluateExpression(fOrTerms[term], i)) {
					orFlags |= uint32_t(1u) << term;
				}
			}
		}
		fOrFlagsLut[i] = orFlags;

	}

	return;
}

uint32_t TriggerMap::LookUpOrFlags(uint32_t val)
{
	if (val < fMapSize) {
		return fOrFlagsLut[val];
	}
	return 0;
}

bool TriggerMap::LookUp(uint32_t val)
{
	if (val < fMapSize) {
		return fLut[val];
	} else {
		return false;
	}
}

void TriggerMap::Dump()
{
	for (unsigned int i = 0 ;  i < fMapSize ; i++) {
		if ((i % 32) == 0) std::cout << std::endl << std::setw(6) << i << " : ";
		if ((i % 16) == 0) std::cout << " ";
		std::cout << fLut[i] << " ";
	}
	std::cout << std::endl;
	return;
}

#ifdef TRIGGERMAP_TEST_MAIN
int main(int argc, char *argv[])
{
	std::string form;
	if (argc > 1) {
		form = argv[1];
	} else {
		form = "0 1 & 2 3 & | 4 5 & | 6 7 & 8 9 & | &";
	}

	TriggerMap trig;

	trig.MakeTable(form);
	for (uint32_t i = 0 ; i < 32 ; i++) {
		std::cout  << trig.LookUp(i);
	}
	std::cout << std::endl;
	
	trig.Dump();
	
	return 0;
}
#endif

#endif //#ifdef TriggerMap_cxx
