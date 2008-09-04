/* Copyright (C) 2003 MySQL AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#include <SectionReader.hpp>
#include <TransporterDefinitions.hpp>
#include "LongSignal.hpp"

#if 0
  Uint32 m_len;
  class SectionSegmentPool & m_pool;
  class SectionSegment * m_head;
  class SectionSegment * m_currentPos;
#endif

SectionReader::SectionReader
(struct SegmentedSectionPtr & ptr, class SectionSegmentPool & pool)
  : m_pool(pool)
{
  if(ptr.p == 0){
    m_pos = 0;
    m_len = 0;
    m_headI= RNIL;
    m_head = 0;
    m_currI= RNIL;
    m_currentSegment = 0;
  } else {
    m_pos = 0;
    m_len = ptr.p->m_sz;
    m_headI= ptr.i;
    m_head = ptr.p;
    m_currI= ptr.i;
    m_currentSegment = ptr.p;
  }
}

SectionReader::SectionReader
(Uint32 firstSectionIVal, class SectionSegmentPool& pool)
  : m_pool(pool)
{
  SectionSegment* firstSeg= m_pool.getPtr(firstSectionIVal);
  
  m_pos = 0;
  m_len = firstSeg->m_sz;
  m_headI= m_currI= firstSectionIVal;
  m_head = m_currentSegment = firstSeg;
}

void
SectionReader::reset(){
  m_pos = 0;
  m_currI= m_headI;
  m_currentSegment = m_head;
}

bool
SectionReader::step(Uint32 len){
  if(m_pos + len >= m_len) {
    m_pos++;
    return false;
  }
  while(len > SectionSegment::DataLength){
    m_currI= m_currentSegment->m_nextSegment;
    m_currentSegment = m_pool.getPtr(m_currI);

    len -= SectionSegment::DataLength;
    m_pos += SectionSegment::DataLength;
  }

  Uint32 ind = m_pos % SectionSegment::DataLength;
  while(len > 0){
    len--;
    m_pos++;

    ind++;
    if(ind == SectionSegment::DataLength){
      ind = 0;
      m_currI= m_currentSegment->m_nextSegment;
      m_currentSegment = m_pool.getPtr(m_currI);
    }
  }
  return true;
}

bool
SectionReader::getWord(Uint32 * dst){
  if (peekWord(dst)) {
    step(1);
    return true;
  }
  return false;
}

bool
SectionReader::peekWord(Uint32 * dst) const {
  if(m_pos < m_len){
    Uint32 ind = m_pos % SectionSegment::DataLength;
    * dst = m_currentSegment->theData[ind];
    return true;
  }
  return false;
}

bool
SectionReader::peekWords(Uint32 * dst, Uint32 len) const {
  if(m_pos + len > m_len)
    return false;

  Uint32 ind = (m_pos % SectionSegment::DataLength);
  Uint32 left = SectionSegment::DataLength - ind;
  SectionSegment * p = m_currentSegment;

  while(len > left){
    memcpy(dst, &p->theData[ind], 4 * left);
    dst += left;
    len -= left;
    ind = 0;
    left = SectionSegment::DataLength;
    p = m_pool.getPtr(p->m_nextSegment);
  }

  memcpy(dst, &p->theData[ind], 4 * len);
  return true;
}

bool
SectionReader::getWords(Uint32 * dst, Uint32 len){
  if(m_pos + len > m_len)
    return false;

  /* Use getWordsPtr to correctly traverse segments */

  while (len > 0)
  {
    const Uint32* readPtr;
    Uint32 readLen;

    if (!getWordsPtr(len,
                     readPtr,
                     readLen))
      return false;
    
    memcpy(dst, readPtr, readLen << 2);
    len-= readLen;
    dst+= readLen;
  }

  return true;
}

bool
SectionReader::getWordsPtr(Uint32 maxLen,
                           const Uint32*& readPtr,
                           Uint32& actualLen)
{
  if(m_pos >= m_len)
    return false;

  /* We return a pointer to the current position,
   * with length the minimum of
   *  - significant words remaining in the whole section
   *  - space remaining in the current segment
   *  - maxLen from caller
   */
  const Uint32 sectionRemain= m_len - m_pos;
  const Uint32 startInd = (m_pos % SectionSegment::DataLength);
  const Uint32 segmentSpace = SectionSegment::DataLength - startInd;
  SectionSegment * p = m_currentSegment;

  const Uint32 remain= MIN(sectionRemain, segmentSpace);
  actualLen= MIN(remain, maxLen);
  readPtr= &p->theData[startInd];

  /* If we've read everything in this segment, and
   * there's another one, move onto it ready for 
   * next time
   */
  if (((startInd + actualLen) == SectionSegment::DataLength) &&
      (p->m_nextSegment != RNIL))
  {
    m_currI= p->m_nextSegment;
    m_currentSegment= m_pool.getPtr(m_currI);
  }

  m_pos += actualLen;
  return true;
};

bool
SectionReader::getWordsPtr(const Uint32*& readPtr,
                           Uint32& actualLen)
{
  /* Cannot have more than SectionSegment::DataLength
   * contiguous words
   */
  return getWordsPtr(SectionSegment::DataLength,
                     readPtr,
                     actualLen);
}


SectionReader::PosInfo
SectionReader::getPos()
{
  PosInfo pi;
  pi.currPos= m_pos;
  pi.currIVal= m_currI;
  
  return pi;
}

bool
SectionReader::setPos(PosInfo posInfo)
{
  if (posInfo.currPos > m_len)
    return false;
  
  if (posInfo.currIVal == RNIL)
  {
    if (posInfo.currPos > 0)
      return false;
    m_currentSegment= 0;
  }
  else
  {
    assert(segmentContainsPos(posInfo));
    
    m_currentSegment= m_pool.getPtr(posInfo.currIVal);
  }

  m_pos= posInfo.currPos;
  m_currI= posInfo.currIVal;

  return true;
}

bool
SectionReader::segmentContainsPos(PosInfo posInfo)
{
  /* This is a check that the section referenced 
   * by this SectionReader contains the position 
   * given at the section given.
   * It should not be run in-production
   */
  Uint32 IVal= m_headI;
  Uint32 pos= posInfo.currPos;

  while (pos >= SectionSegment::DataLength)
  {
    /* Get next segment */
    SectionSegment* seg= m_pool.getPtr(IVal);

    IVal= seg->m_nextSegment;
    pos-= SectionSegment::DataLength;
  }

  return (IVal == posInfo.currIVal);
}
