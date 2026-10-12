package kx

import kotlinx.html.*
import kotlinx.html.stream.appendHTML
import org.openjdk.jmh.annotations.*

class Stock(val symbol: String, val url: String, val name: String, val price: String, val change: String, val ratio: String)

val stocks = listOf(
    Stock("ADBE", "http://www.adobe.com", "Adobe Systems", "39.26", "0.13", "0.33"),
    Stock("AMD", "http://www.amd.com", "Advanced Micro Devices", "16.22", "0.17", "1.06"),
    Stock("AMZN", "http://www.amazon.com", "Amazon.com", "36.85", "-0.23", "-0.62"),
    Stock("AAPL", "http://www.apple.com", "Apple", "85.38", "-0.87", "-1.01"),
    Stock("BEAS", "http://www.bea.com", "BEA Systems", "12.46", "0.09", "0.73"),
    Stock("CA", "http://www.ca.com", "CA", "24.66", "0.38", "1.57"),
    Stock("CSCO", "http://www.cisco.com", "Cisco Systems", "26.35", "0.13", "0.5"),
    Stock("DELL", "http://www.dell.com/", "Dell", "23.73", "-0.42", "-1.74"),
    Stock("EBAY", "http://www.ebay.com", "eBay", "31.65", "-0.8", "-2.47"),
    Stock("GOOG", "http://www.google.com", "Google", "495.84", "7.75", "1.59"),
    Stock("HPQ", "http://www.hp.com", "Hewlett-Packard", "41.69", "-0.02", "-0.05"),
    Stock("IBM", "http://www.ibm.com", "IBM", "97.45", "-0.06", "-0.06"),
    Stock("INTC", "http://www.intel.com", "Intel", "20.53", "-0.07", "-0.34"),
    Stock("JNPR", "http://www.juniper.net/", "Juniper Networks", "18.96", "0.5", "2.71"),
    Stock("MSFT", "http://www.microsoft.com", "Microsoft", "30.6", "0.15", "0.49"),
    Stock("ORCL", "http://www.oracle.com", "Oracle", "17.15", "0.17", "1.1"),
    Stock("SAP", "http://www.sap.com", "SAP", "46.2", "-0.16", "-0.35"),
    Stock("STX", "http://www.seagate.com/", "Seagate Technology", "27.35", "-0.36", "-1.3"),
    Stock("SUNW", "http://www.sun.com", "Sun Microsystems", "6.33", "-0.01", "-0.16"),
    Stock("YHOO", "http://www.yahoo.com", "Yahoo", "28.04", "-0.17", "-0.6"),
)

const val CSS = "body{color:#333333;line-height:150%}thead{font-weight:bold;background-color:#CCCCCC}.odd{background-color:#FFCCCC}.even{background-color:#CCCCFF}.minus{color:#FF0000}"

class Item(val rowClass: String, val index: String, val negativeClass: String?, val value: Stock)

val items = stocks.mapIndexed { i, s ->
    val minus = s.change.startsWith("-") || s.change == "0"
    Item(if (i % 2 == 0) "even" else "odd", (i + 1).toString(), if (minus) "minus" else null, s)
}

fun page(sb: StringBuilder, rows: TBODY.() -> Unit) {
    sb.append("<!DOCTYPE html>")
    sb.appendHTML(prettyPrint = false).html {
        head {
            title { +"Stock Prices" }
            meta { httpEquiv = "Content-Type"; content = "text/html; charset=UTF-8" }
            link { rel = "shortcut icon"; href = "/images/favicon.ico" }
            link { rel = "stylesheet"; type = "text/css"; href = "/css/style.css"; media = "all" }
            script { type = "text/javascript"; src = "/js/util.js" }
            style { unsafe { +CSS } }
        }
        body {
            h1 { +"Stock Prices" }
            table {
                thead { tr { th { +"#" }; th { +"symbol" }; th { +"name" }; th { +"price" }; th { +"change" }; th { +"ratio" } } }
                tbody { rows() }
            }
        }
    }
}

fun renderFinished(capacity: Int): String {
    val sb = StringBuilder(capacity)
    page(sb) {
        for (it in items) {
            tr(it.rowClass) {
                td { +it.index }
                td { a("/stocks/${it.value.symbol}") { +it.value.symbol } }
                td { a(it.value.url) { +it.value.name } }
                td { strong { +it.value.price } }
                td(it.negativeClass) { +it.value.change }
                td(it.negativeClass) { +it.value.ratio }
            }
        }
    }
    return sb.toString()
}

fun render(capacity: Int): String {
    val sb = StringBuilder(capacity)
    sb.append("<!DOCTYPE html>")
    sb.appendHTML(prettyPrint = false).html {
        head {
            title { +"Stock Prices" }
            meta { httpEquiv = "Content-Type"; content = "text/html; charset=UTF-8" }
            link { rel = "shortcut icon"; href = "/images/favicon.ico" }
            link { rel = "stylesheet"; type = "text/css"; href = "/css/style.css"; media = "all" }
            script { type = "text/javascript"; src = "/js/util.js" }
            style { unsafe { +CSS } }
        }
        body {
            h1 { +"Stock Prices" }
            table {
                thead { tr { th { +"#" }; th { +"symbol" }; th { +"name" }; th { +"price" }; th { +"change" }; th { +"ratio" } } }
                tbody {
                    stocks.forEachIndexed { i, s ->
                        val minus = s.change.startsWith("-") || s.change == "0"
                        tr(if (i % 2 == 0) "even" else "odd") {
                            td { +(i + 1).toString() }
                            td { a("/stocks/${s.symbol}") { +s.symbol } }
                            td { a(s.url) { +s.name } }
                            td { strong { +s.price } }
                            td(if (minus) "minus" else null) { +s.change }
                            td(if (minus) "minus" else null) { +s.ratio }
                        }
                    }
                }
            }
        }
    }
    return sb.toString()
}

@State(Scope.Thread)
open class Stocks {
    var last = 64
    @Benchmark
    fun per_render(): String { val s = render(last); last = s.length; return s }
    @Benchmark
    fun finished(): String { val s = renderFinished(last); last = s.length; return s }
}

fun main() { check(render(64) == renderFinished(64)); print(renderFinished(64)) }
