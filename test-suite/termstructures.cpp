
/*
 Copyright (C) 2003 RiskMap srl

 This file is part of QuantLib, a free-software/open-source library
 for financial quantitative analysts and developers - http://quantlib.org/

 QuantLib is free software: you can redistribute it and/or modify it under the
 terms of the QuantLib license.  You should have received a copy of the
 license along with this program; if not, please email quantlib-dev@lists.sf.net
 The license is also available online at http://quantlib.org/html/license.html

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the license for more details.
*/

#include "termstructures.hpp"
#include "utilities.hpp"
#include <ql/TermStructures/flatforward.hpp>
#include <ql/TermStructures/piecewiseflatforward.hpp>
#include <ql/TermStructures/impliedtermstructure.hpp>
#include <ql/TermStructures/forwardspreadedtermstructure.hpp>
#include <ql/TermStructures/zerospreadedtermstructure.hpp>
#include <ql/Calendars/target.hpp>
#include <ql/Calendars/nullcalendar.hpp>
#include <ql/DayCounters/actual360.hpp>
#include <ql/DayCounters/thirty360.hpp>
#include <ql/Math/comparison.hpp>

using namespace QuantLib;
using namespace boost::unit_test_framework;

namespace {

    // global data

    Calendar calendar_;
    Integer settlementDays_;
    boost::shared_ptr<YieldTermStructure> termStructure_;

    // utilities

    struct Datum {
        Integer n;
        TimeUnit units;
        Rate rate;
    };

    void setup() {
        calendar_ = TARGET();
        settlementDays_ = 2;
        Date today = calendar_.adjust(Date::todaysDate());
        Settings::instance().setEvaluationDate(today);
        Date settlement = calendar_.advance(today,settlementDays_,Days);
        Datum depositData[] = {
            { 1, Months, 4.581 },
            { 2, Months, 4.573 },
            { 3, Months, 4.557 },
            { 6, Months, 4.496 },
            { 9, Months, 4.490 }
        };
        Datum swapData[] = {
            {  1, Years, 4.54 },
            {  5, Years, 4.99 },
            { 10, Years, 5.47 },
            { 20, Years, 5.89 },
            { 30, Years, 5.96 }
        };
        Size deposits = LENGTH(depositData),
            swaps = LENGTH(swapData);

        std::vector<boost::shared_ptr<RateHelper> >
            instruments(deposits+swaps);
        Size i;
        for (i=0; i<deposits; i++) {
            instruments[i] = boost::shared_ptr<RateHelper>(
                 new DepositRateHelper(depositData[i].rate/100,
                                       depositData[i].n, depositData[i].units,
                                       settlementDays_, calendar_,
                                       ModifiedFollowing, Actual360()));
        }
        for (i=0; i<swaps; i++) {
            instruments[i+deposits] = boost::shared_ptr<RateHelper>(
                          new SwapRateHelper(swapData[i].rate/100,
                                             swapData[i].n, swapData[i].units,
                                             settlementDays_, calendar_,
                                             Annual, Unadjusted, Thirty360(),
                                             Semiannual, ModifiedFollowing));
        }
        termStructure_ = boost::shared_ptr<YieldTermStructure>(
                new PiecewiseFlatForward(settlement,instruments,Actual360()));
    }

    void teardown() {
        Settings::instance().setEvaluationDate(Date());
    }

}


void TermStructureTest::testReferenceChange() {

    BOOST_MESSAGE("Testing term structure against evaluation date change...");

    QL_TEST_BEGIN
    QL_TEST_SETUP

    termStructure_ = boost::shared_ptr<YieldTermStructure>(
         new FlatForward(settlementDays_, NullCalendar(), 0.03, Actual360()));

    Date today = Settings::instance().evaluationDate();
    Integer days[] = { 10, 30, 60, 120, 360, 720 };
    Integer i;

    std::vector<DiscountFactor> expected(LENGTH(days));
    for (i=0; i<LENGTH(days); i++)
        expected[i] = termStructure_->discount(today+days[i]);

    Settings::instance().setEvaluationDate(today+30);
    std::vector<DiscountFactor> calculated(LENGTH(days));
    for (i=0; i<LENGTH(days); i++)
        calculated[i] = termStructure_->discount(today+30+days[i]);

    for (i=0; i<LENGTH(days); i++) {
        if (!close(expected[i],calculated[i]))
            BOOST_FAIL("Discount at "
                       + IntegerFormatter::toString(days[i]) + "days:\n"
                       "    before date change: "
                       + DecimalFormatter::toString(expected[i],12) + "\n"
                       "    after date change:  "
                       + DecimalFormatter::toString(calculated[i],12));
    }

    QL_TEST_TEARDOWN
}


void TermStructureTest::testImplied() {

    BOOST_MESSAGE("Testing consistency of implied term structure...");

    QL_TEST_BEGIN
    QL_TEST_SETUP

    Real tolerance = 1.0e-10;
    Date today = Settings::instance().evaluationDate();
    Date newToday = today.plusYears(3);
    Date newSettlement = calendar_.advance(newToday,settlementDays_,Days);
    Date testDate = newSettlement.plusYears(5);
    boost::shared_ptr<YieldTermStructure> implied(
        new ImpliedTermStructure(Handle<YieldTermStructure>(termStructure_),
                                 newSettlement));
    DiscountFactor baseDiscount = termStructure_->discount(newSettlement);
    DiscountFactor discount = termStructure_->discount(testDate);
    DiscountFactor impliedDiscount = implied->discount(testDate);
    if (QL_FABS(discount - baseDiscount*impliedDiscount) > tolerance)
        BOOST_FAIL(
            "unable to reproduce discount from implied curve\n"
            "    calculated: "
            + DecimalFormatter::toString(baseDiscount*impliedDiscount,10)+"\n"
            "    expected:   "
            + DecimalFormatter::toString(discount,10));

    QL_TEST_TEARDOWN
}

void TermStructureTest::testImpliedObs() {

    BOOST_MESSAGE("Testing observability of implied term structure...");

    QL_TEST_BEGIN
    QL_TEST_SETUP

    Date today = Settings::instance().evaluationDate();
    Date newToday = today.plusYears(3);
    Date newSettlement = calendar_.advance(newToday,settlementDays_,Days);
    Handle<YieldTermStructure> h;
    boost::shared_ptr<YieldTermStructure> implied(
                                  new ImpliedTermStructure(h, newSettlement));
    Flag flag;
    flag.registerWith(implied);
    h.linkTo(termStructure_);
    if (!flag.isUp())
        BOOST_FAIL("Observer was not notified of term structure change");

    QL_TEST_TEARDOWN
}

void TermStructureTest::testFSpreaded() {

    BOOST_MESSAGE("Testing consistency of forward-spreaded term structure...");

    QL_TEST_BEGIN
    QL_TEST_SETUP

    Real tolerance = 1.0e-10;
    boost::shared_ptr<Quote> me(new SimpleQuote(0.01));
    Handle<Quote> mh(me);
    boost::shared_ptr<YieldTermStructure> spreaded(
        new ForwardSpreadedTermStructure(
            Handle<YieldTermStructure>(termStructure_),mh));
    Date testDate = termStructure_->referenceDate().plusYears(5);
    Rate forward = termStructure_->instantaneousForward(testDate);
    Rate spreadedForward = spreaded->instantaneousForward(testDate);
    if (QL_FABS(forward - (spreadedForward-me->value())) > tolerance)
        BOOST_FAIL(
            "unable to reproduce forward from spreaded curve\n"
            "    calculated: "
            + RateFormatter::toString(spreadedForward-me->value(),10) + "\n"
            "    expected:   "
            + RateFormatter::toString(forward,10));

    QL_TEST_TEARDOWN
}

void TermStructureTest::testFSpreadedObs() {

    BOOST_MESSAGE("Testing observability of forward-spreaded "
                  "term structure...");

    QL_TEST_BEGIN
    QL_TEST_SETUP

    boost::shared_ptr<SimpleQuote> me(new SimpleQuote(0.01));
    Handle<Quote> mh(me);
    Handle<YieldTermStructure> h;
    boost::shared_ptr<YieldTermStructure> spreaded(
        new ForwardSpreadedTermStructure(h,mh));
    Flag flag;
    flag.registerWith(spreaded);
    h.linkTo(termStructure_);
    if (!flag.isUp())
        BOOST_FAIL("Observer was not notified of term structure change");
    flag.lower();
    me->setValue(0.005);
    if (!flag.isUp())
        BOOST_FAIL("Observer was not notified of spread change");

    QL_TEST_TEARDOWN
}

void TermStructureTest::testZSpreaded() {

    BOOST_MESSAGE("Testing consistency of zero-spreaded term structure...");

    QL_TEST_BEGIN
    QL_TEST_SETUP

    Real tolerance = 1.0e-10;
    boost::shared_ptr<Quote> me(new SimpleQuote(0.01));
    Handle<Quote> mh(me);
    boost::shared_ptr<YieldTermStructure> spreaded(
        new ZeroSpreadedTermStructure(
            Handle<YieldTermStructure>(termStructure_),mh));
    Date testDate = termStructure_->referenceDate().plusYears(5);
    Rate zero = termStructure_->zeroYield(testDate);
    Rate spreadedZero = spreaded->zeroYield(testDate);
    if (QL_FABS(zero - (spreadedZero-me->value())) > tolerance)
        BOOST_FAIL(
            "unable to reproduce zero yield from spreaded curve\n"
            "    calculated: "
            + RateFormatter::toString(spreadedZero-me->value(),10) + "\n"
            "    expected:   "
            + RateFormatter::toString(zero,10));

    QL_TEST_TEARDOWN
}

void TermStructureTest::testZSpreadedObs() {

    BOOST_MESSAGE("Testing observability of zero-spreaded term structure...");

    QL_TEST_BEGIN
    QL_TEST_SETUP

    boost::shared_ptr<SimpleQuote> me(new SimpleQuote(0.01));
    Handle<Quote> mh(me);
    Handle<YieldTermStructure> h;
    boost::shared_ptr<YieldTermStructure> spreaded(
        new ZeroSpreadedTermStructure(h,mh));
    Flag flag;
    flag.registerWith(spreaded);
    h.linkTo(termStructure_);
    if (!flag.isUp())
        BOOST_FAIL("Observer was not notified of term structure change");
    flag.lower();
    me->setValue(0.005);
    if (!flag.isUp())
        BOOST_FAIL("Observer was not notified of spread change");

    QL_TEST_TEARDOWN
}


test_suite* TermStructureTest::suite() {
    test_suite* suite = BOOST_TEST_SUITE("Term structure tests");
    suite->add(BOOST_TEST_CASE(&TermStructureTest::testReferenceChange));
    suite->add(BOOST_TEST_CASE(&TermStructureTest::testImplied));
    suite->add(BOOST_TEST_CASE(&TermStructureTest::testImpliedObs));
    suite->add(BOOST_TEST_CASE(&TermStructureTest::testFSpreaded));
    suite->add(BOOST_TEST_CASE(&TermStructureTest::testFSpreadedObs));
    suite->add(BOOST_TEST_CASE(&TermStructureTest::testZSpreaded));
    suite->add(BOOST_TEST_CASE(&TermStructureTest::testZSpreadedObs));
    return suite;
}

