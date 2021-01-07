/*
 * Copyright (C) 2019 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "LineLayoutContext.h"

#if ENABLE(LAYOUT_FORMATTING_CONTEXT)

#include "InlineFormattingContext.h"
#include "LayoutBox.h"
#include "TextUtil.h"

namespace WebCore {
namespace Layout {

static inline bool endsWithSoftWrapOpportunity(const InlineTextItem& currentTextItem, const InlineTextItem& nextInlineTextItem)
{
    ASSERT(!nextInlineTextItem.isWhitespace());
    // We are at the position after a whitespace.
    if (currentTextItem.isWhitespace())
        return true;
    // When both these non-whitespace runs belong to the same layout box, it's guaranteed that
    // they are split at a soft breaking opportunity. See InlineTextItem::moveToNextBreakablePosition.
    if (&currentTextItem.layoutBox() == &nextInlineTextItem.layoutBox())
        return true;
    // Now we need to collect at least 3 adjacent characters to be able to make a decision whether the previous text item ends with breaking opportunity.
    // [ex-][ample] <- second to last[x] last[-] current[a]
    // We need at least 1 character in the current inline text item and 2 more from previous inline items.
    auto previousContent = currentTextItem.layoutBox().textContext()->content;
    auto lineBreakIterator = LazyLineBreakIterator { nextInlineTextItem.layoutBox().textContext()->content };
    auto previousContentLength = previousContent.length();
    // FIXME: We should look into the entire uncommitted content for more text context.
    UChar lastCharacter = previousContentLength ? previousContent[previousContentLength - 1] : 0;
    UChar secondToLastCharacter = previousContentLength > 1 ? previousContent[previousContentLength - 2] : 0;
    lineBreakIterator.setPriorContext(lastCharacter, secondToLastCharacter);
    // Now check if we can break right at the inline item boundary.
    // With the [ex-ample], findNextBreakablePosition should return the startPosition (0).
    // FIXME: Check if there's a more correct way of finding breaking opportunities.
    return !TextUtil::findNextBreakablePosition(lineBreakIterator, 0, nextInlineTextItem.style());
}

static inline bool isAtSoftWrapOpportunity(const InlineItem& current, const InlineItem& next)
{
    // "is at" simple means that there's a soft wrap opportunity right after the [current].
    // [text][ ][text][container start]... (<div>text content<span>..</div>)
    // soft wrap indexes: 0 and 1 definitely, 2 depends on the content after the [container start].

    // https://drafts.csswg.org/css-text-3/#line-break-details
    // Figure out if the new incoming content puts the uncommitted content on a soft wrap opportunity.
    // e.g. [container start][prior_continuous_content][container end] (<span>prior_continuous_content</span>)
    // An incoming <img> box would enable us to commit the "<span>prior_continuous_content</span>" content
    // but an incoming text content would not necessarily.
    ASSERT(current.isText() || current.isBox());
    ASSERT(next.isText() || next.isBox());
    if (current.isText() && next.isText()) {
        auto& currentInlineTextItem = downcast<InlineTextItem>(current);
        auto& nextInlineTextItem = downcast<InlineTextItem>(next);
        if (currentInlineTextItem.isWhitespace()) {
            // [ ][text] : after [whitespace] position is a soft wrap opportunity.
            return true;
        }
        if (nextInlineTextItem.isWhitespace()) {
            // [text][ ] (<span>text</span> )
            // white-space: break-spaces: line breaking opportunity exists after every preserved white space character, but not before.
            return nextInlineTextItem.style().whiteSpace() != WhiteSpace::BreakSpaces;
        }
        if (current.style().lineBreak() == LineBreak::Anywhere || next.style().lineBreak() == LineBreak::Anywhere) {
            // There is a soft wrap opportunity around every typographic character unit, including around any punctuation character
            // or preserved white spaces, or in the middle of words.
            return true;
        }
        // Both current and next items are non-whitespace text.
        // [text][text] : is a continuous content.
        // [text-][text] : after [hyphen] position is a soft wrap opportunity.
        return endsWithSoftWrapOpportunity(currentInlineTextItem, nextInlineTextItem);
    }
    if (current.isBox() || next.isBox()) {
        // [text][container start][container end][inline box] (text<span></span><img>) : there's a soft wrap opportunity between the [text] and [img].
        // The line breaking behavior of a replaced element or other atomic inline is equivalent to an ideographic character.
        return true;
    }
    ASSERT_NOT_REACHED();
    return true;
}

static inline size_t nextWrapOpportunity(const InlineItems& inlineContent, size_t startIndex, const LineLayoutContext::InlineItemRange layoutRange)
{
    // 1. Find the start candidate by skipping leading non-content items e.g <span><span>start : skip "<span><span>"
    // 2. Find the end candidate by skipping non-content items inbetween e.g. <span><span>start</span>end: skip "</span>"
    // 3. Check if there's a soft wrap opportunity between the 2 candidate inline items and repeat.
    // 4. Any force line break inbetween is considered as a wrap opportunity.

    // [ex-][container start][container end][float][ample] (ex-<span></span><div style="float:left"></div>ample) : wrap index is at [ex-].
    // [ex][container start][amp-][container start][le] (ex<span>amp-<span>ample) : wrap index is at [amp-].
    // [ex-][container start][line break][ample] (ex-<span><br>ample) : wrap index is after [br].
    auto isAtLineBreak = false;

    auto inlineItemIndexWithContent = [&] (auto index) {
        // Break at the first text/box/line break inline item.
        for (; index < layoutRange.end; ++index) {
            auto& inlineItem = inlineContent[index];
            if (inlineItem.isText() || inlineItem.isBox())
                return index;
            if (inlineItem.isLineBreak()) {
                isAtLineBreak = true;
                return index;
            }
        }
        return layoutRange.end;
    };

    // Start at the first inline item with content.
    // [container start][ex-] : start at [ex-]
    auto startContentIndex = inlineItemIndexWithContent(startIndex);
    if (isAtLineBreak) {
        // Content starts with a line break. The wrap position is after the line break.
        return startContentIndex + 1;
    }

    while (startContentIndex < layoutRange.end) {
        // 1. Find the next inline item with content.
        // 2. Check if there's a soft wrap opportunity between the start and the next inline item.
        auto nextContentIndex = inlineItemIndexWithContent(startContentIndex + 1);
        if (nextContentIndex == layoutRange.end)
            return nextContentIndex;
        if (isAtLineBreak) {
            // We always stop at line breaks. The wrap position is after the line break.
            return nextContentIndex + 1;
        }
        if (isAtSoftWrapOpportunity(inlineContent[startContentIndex], inlineContent[nextContentIndex])) {
            // There's a soft wrap opportunity between the start and the nextContent.
            // Now forward-find from the start position to see where we can actually wrap.
            // [ex-][ample] vs. [ex-][container start][container end][ample]
            // where [ex-] is startContent and [ample] is the nextContent.
            for (auto candidateIndex = startContentIndex + 1; candidateIndex < nextContentIndex; ++candidateIndex) {
                if (inlineContent[candidateIndex].isContainerStart()) {
                    // inline content and [container start] and [container end] form unbreakable content.
                    // ex-<span></span>ample  : wrap opportunity is after "ex-".
                    // ex-</span></span>ample : wrap opportunity is after "ex-</span></span>".
                    // ex-</span><span>ample</span> : wrap opportunity is after "ex-</span>".
                    // ex-<span><span>ample</span></span> : wrap opportunity is after "ex-".
                    return candidateIndex;
                }
            }
            return nextContentIndex;
        }
        startContentIndex = nextContentIndex;
    }
    return layoutRange.end;
}

struct LineCandidateContent {
    void appendInlineContent(const InlineItem&, InlineLayoutUnit logicalWidth);
    void appendLineBreak(const InlineItem& inlineItem) { setTrailingLineBreak(inlineItem); }
    void appendFloat(const InlineItem& inlineItem) { m_floats.append(&inlineItem); }

    bool hasIntrusiveFloats() const { return !m_floats.isEmpty(); }
    const LineBreaker::RunList& inlineRuns() const { return m_inlineRuns; }
    InlineLayoutUnit inlineContentLogicalWidth() const { return m_inlineContentLogicalWidth; }
    const LineLayoutContext::FloatList& floats() const { return m_floats; }

    const InlineItem* trailingLineBreak() const { return m_trailingLineBreak; }

    void reset();

private:
    void setTrailingLineBreak(const InlineItem& lineBreakItem) { m_trailingLineBreak = &lineBreakItem; }

    InlineLayoutUnit m_inlineContentLogicalWidth { 0 };
    LineBreaker::RunList m_inlineRuns;
    LineLayoutContext::FloatList m_floats;
    const InlineItem* m_trailingLineBreak { nullptr };
};

inline void LineCandidateContent::appendInlineContent(const InlineItem& inlineItem, InlineLayoutUnit logicalWidth)
{
    m_inlineContentLogicalWidth += logicalWidth;
    m_inlineRuns.append({ inlineItem, logicalWidth });
}

inline void LineCandidateContent::reset()
{
    m_inlineContentLogicalWidth = 0;
    m_inlineRuns.clear();
    m_floats.clear();
    m_trailingLineBreak = nullptr;
}

InlineLayoutUnit LineLayoutContext::inlineItemWidth(const InlineItem& inlineItem, InlineLayoutUnit contentLogicalLeft) const
{
    if (is<InlineTextItem>(inlineItem)) {
        auto& inlineTextItem = downcast<InlineTextItem>(inlineItem);
        if (auto contentWidth = inlineTextItem.width())
            return *contentWidth;
        auto end = inlineTextItem.isCollapsible() ? inlineTextItem.start() + 1 : inlineTextItem.end();
        return TextUtil::width(inlineTextItem, inlineTextItem.start(), end, contentLogicalLeft);
    }

    if (inlineItem.isLineBreak())
        return 0;

    auto& layoutBox = inlineItem.layoutBox();
    auto& boxGeometry = m_inlineFormattingContext.geometryForBox(layoutBox);

    if (layoutBox.isFloatingPositioned())
        return boxGeometry.marginBoxWidth();

    if (layoutBox.replaced())
        return boxGeometry.width();

    if (inlineItem.isContainerStart())
        return boxGeometry.marginStart() + boxGeometry.borderLeft() + boxGeometry.paddingLeft().valueOr(0);

    if (inlineItem.isContainerEnd())
        return boxGeometry.marginEnd() + boxGeometry.borderRight() + boxGeometry.paddingRight().valueOr(0);

    // Non-replaced inline box (e.g. inline-block)
    return boxGeometry.width();
}

static inline bool isLineConsideredEmpty(const LineBuilder& line)
{
    return line.isVisuallyEmpty() && !line.hasIntrusiveFloat();
}

LineLayoutContext::LineLayoutContext(const InlineFormattingContext& inlineFormattingContext, const Container& formattingContextRoot, const InlineItems& inlineItems)
    : m_inlineFormattingContext(inlineFormattingContext)
    , m_formattingContextRoot(formattingContextRoot)
    , m_inlineItems(inlineItems)
{
}

LineLayoutContext::LineContent LineLayoutContext::layoutLine(LineBuilder& line, const InlineItemRange layoutRange, Optional<unsigned> partialLeadingContentLength)
{
    auto reset = [&] {
        ASSERT(m_floats.isEmpty());
        m_partialTrailingTextItem = { };
        m_partialLeadingTextItem = { };
    };
    reset();
    auto lineBreaker = LineBreaker { };
    auto currentItemIndex = layoutRange.start;
    unsigned committedInlineItemCount = 0;
    auto candidateContent = LineCandidateContent { };
    while (currentItemIndex < layoutRange.end) {
        // 1. Collect the set of runs that we can commit to the line as one entity e.g. <span>text_and_span_start_span_end</span>.
        // 2. Apply floats and shrink the available horizontal space e.g. <span>intru_<div style="float: left"></div>sive_float</span>.
        // 3. Check if the content fits the line and commit the content accordingly (full, partial or not commit at all).
        // 4. Return if we are at the end of the line either by not being able to fit more content or because of an explicit line break.
        nextContentForLine(candidateContent, currentItemIndex, layoutRange, partialLeadingContentLength, line.lineBox().logicalWidth());
        if (candidateContent.hasIntrusiveFloats()) {
            // Add floats first because they shrink the available horizontal space for the rest of the content.
            auto result = tryAddingFloatItems(line, candidateContent.floats());
            committedInlineItemCount += result.committedCount;
            if (result.isEndOfLine == LineBreaker::IsEndOfLine::Yes) {
                // Floats take up all the horizontal space.
                return close(line, layoutRange, committedInlineItemCount, { });
            }
        }
        if (!candidateContent.inlineRuns().isEmpty()) {
            // Now check if we can put this content on the current line.
            auto result = tryAddingInlineItems(lineBreaker, line, candidateContent);
            if (result.revertTo) {
                ASSERT(!result.committedCount);
                ASSERT(result.isEndOfLine == LineBreaker::IsEndOfLine::Yes);
                // An earlier line wrapping opportunity turned out to be the final breaking position.
                rebuildLineForRevert(line, *result.revertTo, layoutRange);
            }
            committedInlineItemCount += result.committedCount;
            if (result.isEndOfLine == LineBreaker::IsEndOfLine::Yes) {
                // We can't place any more items on the current line.
                return close(line, layoutRange, committedInlineItemCount, result.partialContent);
            }
        } else if (auto* trailingLineBreak = candidateContent.trailingLineBreak()) {
            line.append(*trailingLineBreak, 0);
            return close(line, layoutRange, ++committedInlineItemCount, { });
        }
        currentItemIndex = layoutRange.start + committedInlineItemCount;
        partialLeadingContentLength = { };
    }
    // Looks like we've run out of runs.
    return close(line, layoutRange, committedInlineItemCount, { });
}

LineLayoutContext::LineContent LineLayoutContext::close(LineBuilder& line, const InlineItemRange layoutRange, unsigned committedInlineItemCount, Optional<LineContent::PartialContent> partialContent)
{
    ASSERT(committedInlineItemCount || line.hasIntrusiveFloat());
    if (!committedInlineItemCount)
        return LineContent { { }, { }, WTFMove(m_floats), line.close(), line.lineBox() };

    // Adjust hyphenated line count.
    if (partialContent && partialContent->trailingContentNeedsHyphen)
        ++m_successiveHyphenatedLineCount;
    else
        m_successiveHyphenatedLineCount = 0;

    unsigned trailingInlineItemIndex = layoutRange.start + committedInlineItemCount - 1;
    ASSERT(trailingInlineItemIndex < layoutRange.end);
    auto isLastLineWithInlineContent = [&] {
        if (trailingInlineItemIndex == layoutRange.end - 1)
            return LineBuilder::IsLastLineWithInlineContent::Yes;
        if (partialContent)
            return LineBuilder::IsLastLineWithInlineContent::No;
        // Omit floats to see if this is the last line with inline content.
        for (auto i = layoutRange.end; i--;) {
            if (!m_inlineItems[i].isFloat())
                return i == trailingInlineItemIndex ? LineBuilder::IsLastLineWithInlineContent::Yes : LineBuilder::IsLastLineWithInlineContent::No;
        }
        // There has to be at least one non-float item.
        ASSERT_NOT_REACHED();
        return LineBuilder::IsLastLineWithInlineContent::No;
    }();
    return LineContent { trailingInlineItemIndex, partialContent, WTFMove(m_floats), line.close(isLastLineWithInlineContent), line.lineBox() };
}

void LineLayoutContext::nextContentForLine(LineCandidateContent& candidateContent, unsigned currentInlineItemIndex, const InlineItemRange layoutRange, Optional<unsigned> partialLeadingContentLength, InlineLayoutUnit currentLogicalRight)
{
    ASSERT(currentInlineItemIndex < layoutRange.end);
    candidateContent.reset();
    // 1. Simply add any overflow content from the previous line to the candidate content. It's always a text content.
    // 2. Find the next soft wrap position or explicit line break.
    // 3. Collect floats between the inline content.
    auto softWrapOpportunityIndex = nextWrapOpportunity(m_inlineItems, currentInlineItemIndex, layoutRange);
    // softWrapOpportunityIndex == layoutRange.end means we don't have any wrap opportunity in this content.
    ASSERT(softWrapOpportunityIndex <= layoutRange.end);

    if (partialLeadingContentLength) {
        // Handle leading partial content first (split text from the previous line).
        // Construct a partial leading inline item.
        m_partialLeadingTextItem = downcast<InlineTextItem>(m_inlineItems[currentInlineItemIndex]).right(*partialLeadingContentLength);
        auto itemWidth = inlineItemWidth(*m_partialLeadingTextItem, currentLogicalRight);
        candidateContent.appendInlineContent(*m_partialLeadingTextItem, itemWidth);
        currentLogicalRight += itemWidth;
        ++currentInlineItemIndex;
    }

    for (auto index = currentInlineItemIndex; index < softWrapOpportunityIndex; ++index) {
        auto& inlineItem = m_inlineItems[index];
        if (inlineItem.isText() || inlineItem.isContainerStart() || inlineItem.isContainerEnd()) {
            auto inlineItenmWidth = inlineItemWidth(inlineItem, currentLogicalRight);
            candidateContent.appendInlineContent(inlineItem, inlineItenmWidth);
            currentLogicalRight += inlineItenmWidth;
            continue;
        }
        if (inlineItem.isFloat()) {
            // Floats are not part of the line context.
            // FIXME: Check if their width should be added to currentLogicalRight.
            candidateContent.appendFloat(inlineItem);
            continue;
        }
        if (inlineItem.isLineBreak()) {
            candidateContent.appendLineBreak(inlineItem);
            continue;
        }
        ASSERT_NOT_REACHED();
    }
}

LineLayoutContext::Result LineLayoutContext::tryAddingFloatItems(LineBuilder& line, const FloatList& floats)
{
    size_t committedFloatItemCount = 0;
    for (auto& floatItem : floats) {
        auto logicalWidth = inlineItemWidth(*floatItem, { });

        auto lineIsConsideredEmpty = line.isVisuallyEmpty() && !line.hasIntrusiveFloat();
        if (LineBreaker().shouldWrapFloatBox(logicalWidth, line.availableWidth() + line.trimmableTrailingWidth(), lineIsConsideredEmpty))
            return { LineBreaker::IsEndOfLine::Yes, committedFloatItemCount };
        // This float can sit on the current line.
        ++committedFloatItemCount;
        auto& floatBox = floatItem->layoutBox();
        // Shrink available space for current line and move existing inline runs.
        line.setHasIntrusiveFloat();
        if (floatBox.isLeftFloatingPositioned())
            line.moveLogicalLeft(logicalWidth);
        else
            line.moveLogicalRight(logicalWidth);
        m_floats.append(floatItem);
    }
    return { LineBreaker::IsEndOfLine::No, committedFloatItemCount };
}

LineLayoutContext::Result LineLayoutContext::tryAddingInlineItems(LineBreaker& lineBreaker, LineBuilder& line, const LineCandidateContent& candidateContent)
{
    auto shouldDisableHyphenation = [&] {
        auto& style = root().style();
        unsigned limitLines = style.hyphenationLimitLines() == RenderStyle::initialHyphenationLimitLines() ? std::numeric_limits<unsigned>::max() : style.hyphenationLimitLines();
        return m_successiveHyphenatedLineCount >= limitLines;
    };
    // Check if this new content fits.
    auto lineStatus = LineBreaker::LineStatus { line.availableWidth(), line.trimmableTrailingWidth(), line.isTrailingRunFullyTrimmable(), isLineConsideredEmpty(line) };

    if (shouldDisableHyphenation())
        lineBreaker.setHyphenationDisabled();

    auto& candidateRuns = candidateContent.inlineRuns();
    auto result = lineBreaker.shouldWrapInlineContent(candidateRuns, candidateContent.inlineContentLogicalWidth(), lineStatus);
    if (result.action == LineBreaker::Result::Action::Keep) {
        // This continuous content can be fully placed on the current line.
        for (auto& run : candidateRuns)
            line.append(run.inlineItem, run.logicalWidth);
        // Consume trailing line break as well.
        if (auto* lineBreakItem = candidateContent.trailingLineBreak()) {
            line.append(*lineBreakItem, 0);
            return { LineBreaker::IsEndOfLine::Yes, candidateRuns.size() + 1 };
        }
        return { result.isEndOfLine, candidateRuns.size() };
    }
    if (result.action == LineBreaker::Result::Action::Push) {
        // This continuous content can't be placed on the current line. Nothing to commit at this time.
        return { result.isEndOfLine };
    }
    if (result.action == LineBreaker::Result::Action::Revert) {
        // Not only this content can't be placed on the current line, but we even need to revert the line back to an earlier position.
        return { result.isEndOfLine, 0, { }, result.revertTo };
    }
    if (result.action == LineBreaker::Result::Action::Split) {
        // Commit the combination of full and partial content on the current line.
        ASSERT(result.partialTrailingContent);
        commitPartialContent(line, candidateRuns, *result.partialTrailingContent);
        // When splitting multiple runs <span style="word-break: break-all">text</span><span>content</span>, we might end up splitting them at run boundary.
        // It simply means we don't really have a partial run. Partial content yes, but not partial run.
        auto trailingRunIndex = result.partialTrailingContent->trailingRunIndex;
        auto committedInlineItemCount = trailingRunIndex + 1;
        if (!result.partialTrailingContent->partialRun)
            return { result.isEndOfLine, committedInlineItemCount };

        auto partialRun = *result.partialTrailingContent->partialRun;
        auto& trailingInlineTextItem = downcast<InlineTextItem>(candidateRuns[trailingRunIndex].inlineItem);
        auto overflowLength = trailingInlineTextItem.length() - partialRun.length;
        return { result.isEndOfLine, committedInlineItemCount, LineContent::PartialContent { partialRun.needsHyphen, overflowLength } };
    }
    ASSERT_NOT_REACHED();
    return { LineBreaker::IsEndOfLine::No };
}

void LineLayoutContext::commitPartialContent(LineBuilder& line, const LineBreaker::RunList& runs, const LineBreaker::Result::PartialTrailingContent& partialTrailingContent)
{
    for (size_t index = 0; index < runs.size(); ++index) {
        auto& run = runs[index];
        if (partialTrailingContent.trailingRunIndex == index) {
            ASSERT(run.inlineItem.isText());
            // Create and commit partial trailing item.
            if (auto partialRun = partialTrailingContent.partialRun) {
                auto& trailingInlineTextItem = downcast<InlineTextItem>(runs[partialTrailingContent.trailingRunIndex].inlineItem);
                // FIXME: LineBuilder should not hold on to the InlineItem.
                ASSERT(!m_partialTrailingTextItem);
                m_partialTrailingTextItem = trailingInlineTextItem.left(partialRun->length);
                line.append(*m_partialTrailingTextItem, partialRun->logicalWidth);
                return;
            }
            // The partial run is the last content to commit.
            line.append(run.inlineItem, run.logicalWidth);
            return;
        }
        line.append(run.inlineItem, run.logicalWidth);
    }
}

void LineLayoutContext::rebuildLineForRevert(LineBuilder& line, const InlineItem& revertTo, const InlineItemRange layoutRange)
{
    // This is the rare case when the line needs to be reverted to an earlier position.
    line.resetContent();
    auto inlineItemIndex = layoutRange.start;
    InlineLayoutUnit logicalRight = { };
    if (m_partialLeadingTextItem) {
        auto logicalWidth = inlineItemWidth(*m_partialLeadingTextItem, logicalRight);
        line.append(*m_partialLeadingTextItem, logicalWidth);
        logicalRight += logicalWidth;
        if (&revertTo == &m_partialLeadingTextItem.value())
            return;
        ++inlineItemIndex;
    }

    for (; inlineItemIndex < layoutRange.end; ++inlineItemIndex) {
        auto& inlineItem = m_inlineItems[inlineItemIndex];
        auto logicalWidth = inlineItemWidth(inlineItem, logicalRight);
        line.append(inlineItem, logicalWidth);
        logicalRight += logicalWidth;
        if (&inlineItem == &revertTo)
            break;
    }
}

}
}

#endif
