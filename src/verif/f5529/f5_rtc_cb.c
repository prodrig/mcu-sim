/* f5_rtc_cb.c — testf5, F5 (M4): el RTC_A y el Comp_B con la DriverLib de TI.
 *   El RTC_A en calendario, en binario, desde el 31-12-2025 23:59:57 (miércoles)
 *   con el evento de cada minuto y la alarma a las 00:00: el CPU duerme en LPM3
 *   y lo despiertan las dos interrupciones, por RTCIV.
 *   El Comp_B: CB0 (P6.0) en (+) contra la referencia de 1,5 V en (-), con las
 *   dos interrupciones (CBIFG al subir, CBIIFG al bajar); el banco mueve P6.0.
 *   Con el DCO de fábrica (SMCLK = 1 MHz), la UCA1 a 9600; ACLK = REFO. */
#include <msp430.h>
#include <stdint.h>
#include "driverlib.h"

static void pon(char c) { while (!(UCA1IFG & UCTXIFG)) ; UCA1TXBUF = c; }
static void cad(const char* s) { while (*s) pon(*s++); }
static void dec2(unsigned v) { pon('0' + v / 10 % 10); pon('0' + v % 10); }

volatile unsigned ev_tev, ev_alarma, ev_sube, ev_baja, iv_orden;

int main(void) {
    WDT_A_hold(WDT_A_BASE);
    UCS_initClockSignal(UCS_ACLK, UCS_REFOCLK_SELECT, UCS_CLOCK_DIVIDER_1);
    P4SEL |= BIT4 | BIT5;
    UCA1CTL1 = UCSWRST | UCSSEL_2;
    UCA1BR0 = 109; UCA1BR1 = 0; UCA1MCTL = UCBRS_2;     /* 1 048 576 Hz / 9600 [UG5 tabla 34-4] */
    UCA1CTL1 &= ~UCSWRST;

    {
        Calendar c;
        RTC_A_configureCalendarAlarmParam a = {0};
        c.Seconds = 57; c.Minutes = 59; c.Hours = 23; c.DayOfWeek = 3;
        c.DayOfMonth = 31; c.Month = 12; c.Year = 2025;
        RTC_A_initCalendar(RTC_A_BASE, &c, RTC_A_FORMAT_BINARY);
        RTC_A_setCalendarEvent(RTC_A_BASE, RTC_A_CALENDAREVENT_MINUTECHANGE);
        a.minutesAlarm = 0; a.hoursAlarm = 0;
        a.dayOfWeekAlarm = RTC_A_ALARMCONDITION_OFF; a.dayOfMonthAlarm = RTC_A_ALARMCONDITION_OFF;
        RTC_A_configureCalendarAlarm(RTC_A_BASE, &a);
        RTC_A_clearInterrupt(RTC_A_BASE, RTC_A_TIME_EVENT_INTERRUPT | RTC_A_CLOCK_ALARM_INTERRUPT);
        RTC_A_enableInterrupt(RTC_A_BASE, RTC_A_TIME_EVENT_INTERRUPT | RTC_A_CLOCK_ALARM_INTERRUPT);
    }
    {
        Comp_B_initParam p = {0};
        Comp_B_configureReferenceVoltageParam r = {0};
        p.positiveTerminalInput = COMP_B_INPUT0;
        p.negativeTerminalInput = COMP_B_VREF;
        p.powerModeSelect = COMP_B_POWERMODE_NORMALMODE;
        p.outputFilterEnableAndDelayLevel = COMP_B_FILTEROUTPUT_OFF;
        p.invertedOutputPolarity = COMP_B_NORMALOUTPUTPOLARITY;
        Comp_B_init(COMP_B_BASE, &p);
        r.supplyVoltageReferenceBase = COMP_B_VREFBASE1_5V;
        r.lowerLimitSupplyVoltageFractionOf32 = 32;
        r.upperLimitSupplyVoltageFractionOf32 = 32;
        r.referenceAccuracy = COMP_B_ACCURACY_STATIC;
        Comp_B_configureReferenceVoltage(COMP_B_BASE, &r);
        Comp_B_enable(COMP_B_BASE);
        __delay_cycles(100);
        Comp_B_clearInterrupt(COMP_B_BASE, CBIFG | CBIIFG);
        Comp_B_enableInterrupt(COMP_B_BASE, CBIE | CBIIE);
    }
    RTC_A_startClock(RTC_A_BASE);
    cad("listo cb "); pon((CBCTL1 & CBOUT) ? '1' : '0'); pon('\n');
    for (;;) {
        __bis_SR_register(LPM3_bits | GIE);
        if (ev_sube) { ev_sube = 0; cad("cb sube "); pon((CBCTL1 & CBOUT) ? '1' : '0'); pon('\n'); }
        if (ev_baja) { ev_baja = 0; cad("cb baja "); pon((CBCTL1 & CBOUT) ? '1' : '0'); pon('\n'); }
        if (ev_tev && ev_alarma) {
            Calendar c = RTC_A_getCalendarTime(RTC_A_BASE);
            ev_tev = ev_alarma = 0;
            cad("rtc "); dec2(c.Year / 100); dec2(c.Year); pon('-'); dec2(c.Month); pon('-'); dec2(c.DayOfMonth);
            pon(' '); dec2(c.Hours); pon(':'); dec2(c.Minutes); pon(':'); dec2(c.Seconds);
            pon(' '); pon('0' + c.DayOfWeek); cad(iv_orden == 0x46 ? " tev+alarma\n" : " ?\n");
        }
    }
}

__attribute__((interrupt(RTC_VECTOR))) void rtc(void) {
    switch (RTCIV) {
    case RTCIV_RTCTEVIFG: ev_tev = 1; iv_orden = iv_orden << 4 | 4; break;
    case RTCIV_RTCAIFG: ev_alarma = 1; iv_orden = iv_orden << 4 | 6; break;
    default: break;
    }
    __bic_SR_register_on_exit(LPM3_bits);
}
__attribute__((interrupt(COMP_B_VECTOR))) void comp(void) {
    switch (CBIV) {
    case 2: ev_sube = 1; break;
    case 4: ev_baja = 1; break;
    default: break;
    }
    __bic_SR_register_on_exit(LPM3_bits);
}
