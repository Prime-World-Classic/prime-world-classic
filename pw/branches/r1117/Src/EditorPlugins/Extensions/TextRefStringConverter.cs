using System;
using System.ComponentModel;
using System.Globalization;
using EditorLib.Extensions;
using EditorLib.FieldProcessor;
using libdb.DB;
using libdb.Diagnostics;
using EditorLib;

namespace EditorPlugins.Extensions
{
  [ApplicableForDeriver]
  public class TextRefStringConverter : TypedStringConvertor<TextRef>
  {
    // English translation is shown after the original (so the grid's hover tooltip shows both):
    // "<original>\r\n\r\n[EN]\r\n<english>"
    private const string EnTag = "[EN]";
    private const string EnPrefix = "\r\n\r\n" + EnTag + "\r\n";

    public override bool SuppressProperties { get { return true; } }
    public TextRefStringConverter( IEditorWinApplication application ) : base( application ) { }

    /// <summary>
    /// Splits a grid value back into original and English parts; english is null if there is no [EN] block
    /// </summary>
    private static void SplitEn( string value, out string original, out string english )
    {
      original = value;
      english = null;

      int tagIndex = value.IndexOf( EnTag, StringComparison.Ordinal );
      if ( tagIndex < 0 )
        return;

      // inverse of EnPrefix; the newlines may be lost if the value was edited in a single-line box
      original = value.Substring( 0, tagIndex );
      if ( original.EndsWith( "\r\n\r\n", StringComparison.Ordinal ) )
        original = original.Substring( 0, original.Length - 4 );

      english = value.Substring( tagIndex + EnTag.Length );
      if ( english.StartsWith( "\r\n", StringComparison.Ordinal ) )
        english = english.Substring( 2 );
    }

    protected override TextRef ConvertFromStringInternal( ITypeDescriptorContext context, object instance, CultureInfo culture, string value )
    {
      if ( context.PropertyDescriptor.PropertyType == typeof( TextRef ) )
      {
        TextRef res = context.PropertyDescriptor.GetValue( instance ) as TextRef;
        string original;
        string english;
        SplitEn( value, out original, out english );
        res.FromString( original );
        // no [EN] block in the value: leave the translation as is (it is cleared in the text dialog)
        if ( english != null )
          res.FromStringEn( english );
        return res;
      }
      else
      {
        Log.TraceWarning( "Expected TextRef PropertyType, but got {0}", context.PropertyDescriptor.PropertyType );
        return null;
      }
    }

    protected override string ConvertToString( ITypeDescriptorContext context, object instance, CultureInfo culture, TextRef value )
    {
      if ( value == null )
        return string.Empty;

      string english = value.GetTextEn();
      if ( string.IsNullOrEmpty( english ) )
        return value.ToString();

      return value.ToString() + EnPrefix + english;
    }
  }
}
